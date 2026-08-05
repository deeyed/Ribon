#include "recovery_network.h"

#include <Protocol/PxeBaseCode.h>
#include <Protocol/SimpleNetwork.h>

#include <Ribon/network/tftp.h>

#include <string.h>

/** @brief PXE Base Code operation 한 번이 가질 UEFI-spec upper bound다. */
#define RIBON_UEFI_PXE_OPERATION_DEADLINE_MS 10000u

/** @brief D05 product가 허용하는 service output byte 상한이다. */
#define RIBON_UEFI_NETWORK_OUTPUT_BUDGET (64ull * 1024ull)

/** @brief Firmware protocol enumeration이 사용할 고정 handle 상한이다. */
#define RIBON_UEFI_NETWORK_HANDLE_CAPACITY 8u

/** @brief Ethernet frame과 bounded TFTP block을 담는 고정 buffer byte 수다. */
#define RIBON_UEFI_SNP_FRAME_CAPACITY 1536u

/** @brief SNP polling의 1 tick을 실제 firmware stall에 결합한다. */
#define RIBON_UEFI_SNP_POLL_MICROSECONDS 1000u

/** @brief Ethernet, IPv4, UDP의 v1 fixed header byte 수다. */
#define RIBON_ETHERNET_HEADER_SIZE 14u
#define RIBON_IPV4_HEADER_SIZE 20u
#define RIBON_UDP_HEADER_SIZE 8u

/** @brief Exact firmware network provider의 Boot Services lifetime state다. */
struct RibonUefiRecoveryNetworkContext {
    EFI_BOOT_SERVICES *boot_services;
    EFI_PXE_BASE_CODE_PROTOCOL *pxe;
    EFI_SIMPLE_NETWORK_PROTOCOL *snp;
    const struct RibonRecoveryNetworkProductBinding *binding;
    enum RibonUefiRecoveryNetworkBackend backend;
    uint8_t opened;
    uint8_t started_by_ribon;
    uint8_t initialized_by_ribon;
    uint8_t server_mac_known;
    uint16_t ipv4_identification;
    uint8_t server_mac[6];
    uint8_t transmit_frame[RIBON_UEFI_SNP_FRAME_CAPACITY];
    uint8_t receive_frame[RIBON_UEFI_SNP_FRAME_CAPACITY];
};

static struct RibonUefiRecoveryNetworkContext network_context;

static EFI_GUID pxe_base_code_guid = EFI_PXE_BASE_CODE_PROTOCOL_GUID;
static EFI_GUID simple_network_guid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;

/** @brief Network-order 16-bit integer를 읽는다. */
static uint16_t load_be16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] << 8u) | bytes[1];
}

/** @brief Network-order 16-bit integer를 쓴다. */
static void store_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8u);
    bytes[1] = (uint8_t)value;
}

/** @brief IPv4 header의 one's-complement checksum을 계산한다. */
static uint16_t ipv4_checksum(const uint8_t *bytes, size_t size)
{
    uint32_t sum = 0u;
    size_t index;
    for (index = 0u; index + 1u < size; index += 2u) {
        sum += load_be16(bytes + index);
        sum = (sum & 0xffffu) + (sum >> 16u);
    }
    if ((size & 1u) != 0u) {
        sum += (uint32_t)bytes[size - 1u] << 8u;
    }
    while ((sum >> 16u) != 0u) {
        sum = (sum & 0xffffu) + (sum >> 16u);
    }
    return (uint16_t)~sum;
}

/** @brief EFI_STATUS를 bounded fetch taxonomy로 축약한다. */
static int fetch_status(EFI_STATUS status)
{
    if (status == EFI_TIMEOUT || status == EFI_NO_RESPONSE ||
        status == EFI_NOT_READY) {
        return RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT;
    }
    if (status == EFI_BUFFER_TOO_SMALL || status == EFI_BAD_BUFFER_SIZE) {
        return RIBON_RECOVERY_NETWORK_STATUS_CAPACITY;
    }
    if (status == EFI_UNSUPPORTED) {
        return RIBON_RECOVERY_NETWORK_STATUS_UNSUPPORTED;
    }
    return RIBON_RECOVERY_NETWORK_STATUS_IO;
}

/** @brief SNP surface가 bounded Ethernet backend에 충분한지 검사한다. */
static int snp_protocol_surface_is_valid(
    const EFI_SIMPLE_NETWORK_PROTOCOL *snp)
{
    return snp != NULL && snp->Mode != NULL &&
        snp->Revision >= EFI_SIMPLE_NETWORK_PROTOCOL_REVISION &&
        snp->Start != NULL && snp->Initialize != NULL &&
        snp->ReceiveFilters != NULL && snp->GetStatus != NULL &&
        snp->Transmit != NULL && snp->Receive != NULL &&
        snp->Mode->HwAddressSize == 6u &&
        snp->Mode->MediaHeaderSize == RIBON_ETHERNET_HEADER_SIZE &&
        snp->Mode->MaxPacketSize >= 1500u;
}

/** @brief SNP의 active link identity를 고정 6-byte MAC으로 얻는다. */
static int snp_link_identity(
    const EFI_SIMPLE_NETWORK_PROTOCOL *snp,
    const uint8_t **identity)
{
    static const uint8_t zero_mac[6] = {0};
    if (!snp_protocol_surface_is_valid(snp) || identity == NULL) {
        return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
    }
    if (memcmp(snp->Mode->CurrentAddress.Addr, zero_mac, sizeof(zero_mac)) != 0) {
        *identity = snp->Mode->CurrentAddress.Addr;
        return RIBON_UEFI_RECOVERY_NETWORK_OK;
    }
    if (memcmp(snp->Mode->PermanentAddress.Addr, zero_mac, sizeof(zero_mac)) != 0) {
        *identity = snp->Mode->PermanentAddress.Addr;
        return RIBON_UEFI_RECOVERY_NETWORK_OK;
    }
    return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
}

/**
 * @brief Bounded SNP handle set을 exact-one physical link identity로 축약한다.
 *
 * Firmware가 같은 NIC의 child handle을 중복 게시해도 MAC identity가 같으면 하나의
 * authority다. 서로 다른 MAC이 하나라도 보이면 복수 NIC로 보고 fail-closed한다.
 */
static int locate_one_snp_link(
    EFI_BOOT_SERVICES *boot_services,
    EFI_SIMPLE_NETWORK_PROTOCOL **protocol)
{
    EFI_HANDLE handles[RIBON_UEFI_NETWORK_HANDLE_CAPACITY];
    EFI_SIMPLE_NETWORK_PROTOCOL *selected = NULL;
    const uint8_t *selected_identity = NULL;
    UINTN size = sizeof(handles);
    UINTN index;
    EFI_STATUS status;
    if (boot_services == NULL || protocol == NULL) {
        return RIBON_UEFI_RECOVERY_NETWORK_BAD_ARGUMENT;
    }
    *protocol = NULL;
    memset(handles, 0, sizeof(handles));
    status = boot_services->LocateHandle(
        ByProtocol, &simple_network_guid, NULL, &size, handles);
    if (status == EFI_BUFFER_TOO_SMALL) {
        return RIBON_UEFI_RECOVERY_NETWORK_AMBIGUOUS;
    }
    if (status == EFI_NOT_FOUND) {
        return RIBON_UEFI_RECOVERY_NETWORK_NOT_FOUND;
    }
    if (EFI_ERROR(status) || size == 0u || size > sizeof(handles) ||
        size % sizeof(handles[0]) != 0u) {
        return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
    }
    for (index = 0u; index < size / sizeof(handles[0]); ++index) {
        EFI_SIMPLE_NETWORK_PROTOCOL *candidate = NULL;
        const uint8_t *candidate_identity = NULL;
        status = boot_services->HandleProtocol(
            handles[index], &simple_network_guid, (void **)&candidate);
        if (EFI_ERROR(status) || candidate == NULL ||
            snp_link_identity(candidate, &candidate_identity) !=
                RIBON_UEFI_RECOVERY_NETWORK_OK) {
            return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
        }
        if (selected == NULL) {
            selected = candidate;
            selected_identity = candidate_identity;
            continue;
        }
        if (memcmp(selected_identity, candidate_identity, 6u) != 0) {
            return RIBON_UEFI_RECOVERY_NETWORK_AMBIGUOUS;
        }
        if (candidate->Mode->State > selected->Mode->State) {
            selected = candidate;
            selected_identity = candidate_identity;
        }
    }
    if (selected == NULL) {
        return RIBON_UEFI_RECOVERY_NETWORK_NOT_FOUND;
    }
    *protocol = selected;
    return RIBON_UEFI_RECOVERY_NETWORK_OK;
}

/** @brief MTFTP에 필요한 PXE Base Code 표면이 완전한지 검사한다. */
static int pxe_protocol_surface_is_valid(
    const EFI_PXE_BASE_CODE_PROTOCOL *pxe)
{
    return pxe != NULL && pxe->Mode != NULL &&
        pxe->Revision >= EFI_PXE_BASE_CODE_PROTOCOL_REVISION &&
        pxe->Start != NULL && pxe->SetStationIp != NULL &&
        pxe->Mtftp != NULL;
}

/**
 * @brief Bounded handle set에서 exact-one usable IPv4 PXE provider를 고른다.
 *
 * EDK2는 한 NIC에 IPv4와 IPv6 PXE child handle을 함께 게시할 수 있다. Protocol
 * handle 수가 아니라 product의 IPv4 MTFTP 계약을 만족하는 provider 수를 세며, 실제
 * 복수 NIC처럼 usable IPv4 provider가 둘 이상이면 계속 fail-closed한다.
 */
static int locate_one_ipv4_pxe(
    EFI_BOOT_SERVICES *boot_services,
    EFI_PXE_BASE_CODE_PROTOCOL **protocol)
{
    EFI_HANDLE handles[RIBON_UEFI_NETWORK_HANDLE_CAPACITY];
    EFI_PXE_BASE_CODE_PROTOCOL *selected = NULL;
    UINTN size = sizeof(handles);
    UINTN index;
    EFI_STATUS status;
    if (boot_services == NULL || protocol == NULL) {
        return RIBON_UEFI_RECOVERY_NETWORK_BAD_ARGUMENT;
    }
    *protocol = NULL;
    memset(handles, 0, sizeof(handles));
    status = boot_services->LocateHandle(
        ByProtocol, &pxe_base_code_guid, NULL, &size, handles);
    if (status == EFI_BUFFER_TOO_SMALL) {
        return RIBON_UEFI_RECOVERY_NETWORK_AMBIGUOUS;
    }
    if (status == EFI_NOT_FOUND) {
        return RIBON_UEFI_RECOVERY_NETWORK_NOT_FOUND;
    }
    if (EFI_ERROR(status) || size == 0u || size > sizeof(handles) ||
        size % sizeof(handles[0]) != 0u) {
        return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
    }
    for (index = 0u; index < size / sizeof(handles[0]); ++index) {
        EFI_PXE_BASE_CODE_PROTOCOL *candidate = NULL;
        status = boot_services->HandleProtocol(
            handles[index], &pxe_base_code_guid, (void **)&candidate);
        if (EFI_ERROR(status) || candidate == NULL) {
            return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
        }
        if (!pxe_protocol_surface_is_valid(candidate)) {
            return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
        }
        if (candidate->Mode->UsingIpv6) {
            continue;
        }
        if (selected != NULL) {
            return RIBON_UEFI_RECOVERY_NETWORK_AMBIGUOUS;
        }
        selected = candidate;
    }
    if (selected == NULL) {
        return RIBON_UEFI_RECOVERY_NETWORK_NOT_FOUND;
    }
    *protocol = selected;
    return RIBON_UEFI_RECOVERY_NETWORK_OK;
}

/** @brief Product-selected static client address로 started IPv4 PXE session을 고정한다. */
static int configure_pxe(
    struct RibonUefiRecoveryNetworkContext *context,
    const struct RibonRecoveryNetworkProductBinding *binding)
{
    EFI_IP_ADDRESS station;
    EFI_IP_ADDRESS subnet;
    EFI_STATUS status;
    if (!pxe_protocol_surface_is_valid(context->pxe) ||
        context->pxe->Mode->UsingIpv6) {
        return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
    }
    memset(&station, 0, sizeof(station));
    memset(&subnet, 0, sizeof(subnet));
    memcpy(station.v4.Addr, binding->station_ipv4, sizeof(binding->station_ipv4));
    memcpy(subnet.v4.Addr, binding->subnet_mask_ipv4,
        sizeof(binding->subnet_mask_ipv4));
    if (!context->pxe->Mode->Started) {
        status = context->pxe->Start(context->pxe, FALSE);
        if (EFI_ERROR(status) && status != EFI_ALREADY_STARTED) {
            return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
        }
        context->started_by_ribon = 1u;
    }
    status = context->pxe->SetStationIp(context->pxe, &station, &subnet);
    if (EFI_ERROR(status)) {
        return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
    }
    return RIBON_UEFI_RECOVERY_NETWORK_OK;
}

/** @brief Exact one SNP provider를 initialized Ethernet state로 전이시킨다. */
static int configure_snp(struct RibonUefiRecoveryNetworkContext *context)
{
    EFI_SIMPLE_NETWORK_PROTOCOL *snp = context->snp;
    EFI_STATUS status;
    uint32_t filters = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
        EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST;
    if (!snp_protocol_surface_is_valid(snp)) {
        return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
    }
    if (snp->Mode->State == EfiSimpleNetworkStopped) {
        status = snp->Start(snp);
        if (EFI_ERROR(status) && status != EFI_ALREADY_STARTED) {
            return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
        }
        context->started_by_ribon = 1u;
    }
    if (snp->Mode->State == EfiSimpleNetworkStarted) {
        status = snp->Initialize(snp, 0u, 0u);
        if (EFI_ERROR(status)) {
            return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
        }
        context->initialized_by_ribon = 1u;
    }
    if (snp->Mode->State != EfiSimpleNetworkInitialized ||
        snp->Mode->HwAddressSize != 6u ||
        snp->Mode->MediaHeaderSize != RIBON_ETHERNET_HEADER_SIZE ||
        snp->Mode->MaxPacketSize < 1500u) {
        return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
    }
    status = snp->ReceiveFilters(snp, filters, 0u, FALSE, 0u, NULL);
    if (EFI_ERROR(status) &&
        (snp->Mode->ReceiveFilterSetting & filters) != filters) {
        return RIBON_UEFI_RECOVERY_NETWORK_FIRMWARE_ERROR;
    }
    return RIBON_UEFI_RECOVERY_NETWORK_OK;
}

/** @brief Budget tick 하나를 소비하고 firmware에 1ms를 양보한다. */
static int poll_stall(
    struct RibonUefiRecoveryNetworkContext *context,
    uint32_t *remaining_ticks)
{
    if (context == NULL || remaining_ticks == NULL || *remaining_ticks == 0u ||
        context->boot_services == NULL || context->boot_services->Stall == NULL) {
        return RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT;
    }
    --*remaining_ticks;
    if (EFI_ERROR(context->boot_services->Stall(
            RIBON_UEFI_SNP_POLL_MICROSECONDS))) {
        return RIBON_RECOVERY_NETWORK_STATUS_IO;
    }
    return RIBON_RECOVERY_NETWORK_STATUS_OK;
}

/** @brief Static frame을 전송하고 같은 buffer의 recycle까지 bounded wait한다. */
static int snp_transmit(
    struct RibonUefiRecoveryNetworkContext *context,
    size_t frame_size,
    uint32_t *remaining_ticks)
{
    EFI_STATUS status;
    void *recycled = NULL;
    if (frame_size < RIBON_ETHERNET_HEADER_SIZE ||
        frame_size > sizeof(context->transmit_frame)) {
        return RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT;
    }
    for (;;) {
        status = context->snp->Transmit(context->snp, 0u, frame_size,
            context->transmit_frame, NULL, NULL, NULL);
        if (status != EFI_NOT_READY) {
            break;
        }
        if (poll_stall(context, remaining_ticks) !=
                RIBON_RECOVERY_NETWORK_STATUS_OK) {
            return RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT;
        }
        (void)context->snp->GetStatus(context->snp, NULL, &recycled);
    }
    if (EFI_ERROR(status)) {
        return fetch_status(status);
    }
    do {
        recycled = NULL;
        status = context->snp->GetStatus(context->snp, NULL, &recycled);
        if (EFI_ERROR(status)) {
            return fetch_status(status);
        }
        if (recycled == context->transmit_frame) {
            return RIBON_RECOVERY_NETWORK_STATUS_OK;
        }
        if (poll_stall(context, remaining_ticks) !=
                RIBON_RECOVERY_NETWORK_STATUS_OK) {
            return RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT;
        }
    } while (*remaining_ticks != 0u);
    return RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT;
}

/** @brief One receive attempt를 실행하고 no-packet을 bounded poll로 변환한다. */
static int snp_receive(
    struct RibonUefiRecoveryNetworkContext *context,
    size_t *frame_size,
    uint32_t *remaining_ticks)
{
    for (;;) {
        UINTN size = sizeof(context->receive_frame);
        EFI_STATUS status = context->snp->Receive(
            context->snp, NULL, &size, context->receive_frame,
            NULL, NULL, NULL);
        if (status == EFI_SUCCESS) {
            if (size < RIBON_ETHERNET_HEADER_SIZE ||
                size > sizeof(context->receive_frame)) {
                return RIBON_RECOVERY_NETWORK_STATUS_MALFORMED;
            }
            *frame_size = size;
            return RIBON_RECOVERY_NETWORK_STATUS_OK;
        }
        if (status != EFI_NOT_READY) {
            return fetch_status(status);
        }
        if (poll_stall(context, remaining_ticks) !=
                RIBON_RECOVERY_NETWORK_STATUS_OK) {
            return RIBON_RECOVERY_NETWORK_STATUS_TIMEOUT;
        }
    }
}

/** @brief Ethernet destination/source/type header를 static frame에 쓴다. */
static void build_ethernet_header(
    struct RibonUefiRecoveryNetworkContext *context,
    const uint8_t destination[6],
    uint16_t ether_type)
{
    memcpy(context->transmit_frame, destination, 6u);
    memcpy(context->transmit_frame + 6u,
        context->snp->Mode->CurrentAddress.Addr, 6u);
    store_be16(context->transmit_frame + 12u, ether_type);
}

/** @brief Static IPv4 peer의 Ethernet address를 ARP로 한 번만 resolve한다. */
static int resolve_server_mac(
    struct RibonUefiRecoveryNetworkContext *context,
    uint32_t *remaining_ticks)
{
    static const uint8_t broadcast[6] = {
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    };
    uint8_t *arp = context->transmit_frame + RIBON_ETHERNET_HEADER_SIZE;
    int status;
    if (context->server_mac_known) {
        return RIBON_RECOVERY_NETWORK_STATUS_OK;
    }
    memset(context->transmit_frame, 0, 42u);
    build_ethernet_header(context, broadcast, 0x0806u);
    store_be16(arp, 1u);
    store_be16(arp + 2u, 0x0800u);
    arp[4] = 6u;
    arp[5] = 4u;
    store_be16(arp + 6u, 1u);
    memcpy(arp + 8u, context->snp->Mode->CurrentAddress.Addr, 6u);
    memcpy(arp + 14u, context->binding->station_ipv4, 4u);
    memcpy(arp + 24u, context->binding->server_ipv4, 4u);
    status = snp_transmit(context, 42u, remaining_ticks);
    if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return status;
    }
    for (;;) {
        const uint8_t *frame = context->receive_frame;
        const uint8_t *reply;
        size_t frame_size;
        status = snp_receive(context, &frame_size, remaining_ticks);
        if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
            return status;
        }
        if (frame_size < 42u || load_be16(frame + 12u) != 0x0806u) {
            continue;
        }
        reply = frame + RIBON_ETHERNET_HEADER_SIZE;
        if (load_be16(reply) != 1u || load_be16(reply + 2u) != 0x0800u ||
            reply[4] != 6u || reply[5] != 4u ||
            load_be16(reply + 6u) != 2u ||
            memcmp(reply + 14u, context->binding->server_ipv4, 4u) != 0 ||
            memcmp(reply + 24u, context->binding->station_ipv4, 4u) != 0 ||
            memcmp(reply + 18u,
                context->snp->Mode->CurrentAddress.Addr, 6u) != 0) {
            continue;
        }
        memcpy(context->server_mac, reply + 8u, 6u);
        context->server_mac_known = 1u;
        return RIBON_RECOVERY_NETWORK_STATUS_OK;
    }
}

/** @brief UDP datagram을 fixed Ethernet/IPv4 frame으로 전송한다. */
static int transmit_udp(
    struct RibonUefiRecoveryNetworkContext *context,
    uint16_t source_port,
    uint16_t destination_port,
    const uint8_t *payload,
    size_t payload_size,
    uint32_t *remaining_ticks)
{
    uint8_t *ip = context->transmit_frame + RIBON_ETHERNET_HEADER_SIZE;
    uint8_t *udp = ip + RIBON_IPV4_HEADER_SIZE;
    size_t frame_size = RIBON_ETHERNET_HEADER_SIZE + RIBON_IPV4_HEADER_SIZE +
        RIBON_UDP_HEADER_SIZE + payload_size;
    uint16_t checksum;
    if (payload == NULL || frame_size > sizeof(context->transmit_frame) ||
        payload_size > UINT16_MAX - RIBON_IPV4_HEADER_SIZE -
            RIBON_UDP_HEADER_SIZE) {
        return RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT;
    }
    memset(context->transmit_frame, 0, frame_size);
    build_ethernet_header(context, context->server_mac, 0x0800u);
    ip[0] = 0x45u;
    store_be16(ip + 2u,
        (uint16_t)(RIBON_IPV4_HEADER_SIZE + RIBON_UDP_HEADER_SIZE + payload_size));
    store_be16(ip + 4u, ++context->ipv4_identification);
    ip[8] = 64u;
    ip[9] = 17u;
    memcpy(ip + 12u, context->binding->station_ipv4, 4u);
    memcpy(ip + 16u, context->binding->server_ipv4, 4u);
    checksum = ipv4_checksum(ip, RIBON_IPV4_HEADER_SIZE);
    store_be16(ip + 10u, checksum);
    store_be16(udp, source_port);
    store_be16(udp + 2u, destination_port);
    store_be16(udp + 4u, (uint16_t)(RIBON_UDP_HEADER_SIZE + payload_size));
    memcpy(udp + RIBON_UDP_HEADER_SIZE, payload, payload_size);
    return snp_transmit(context, frame_size, remaining_ticks);
}

/** @brief IPv4/UDP frame을 exact product peer에서 온 payload로 축소한다. */
static int parse_udp(
    struct RibonUefiRecoveryNetworkContext *context,
    size_t frame_size,
    uint16_t client_port,
    uint16_t server_port,
    const uint8_t **payload,
    size_t *payload_size,
    uint16_t *actual_server_port)
{
    const uint8_t *frame = context->receive_frame;
    const uint8_t *ip;
    const uint8_t *udp;
    size_t ip_header_size;
    uint16_t ip_size;
    uint16_t udp_size;
    if (frame_size < RIBON_ETHERNET_HEADER_SIZE + RIBON_IPV4_HEADER_SIZE +
            RIBON_UDP_HEADER_SIZE || load_be16(frame + 12u) != 0x0800u ||
        memcmp(frame + 6u, context->server_mac, 6u) != 0) {
        return RIBON_RECOVERY_NETWORK_STATUS_UNSUPPORTED;
    }
    ip = frame + RIBON_ETHERNET_HEADER_SIZE;
    ip_header_size = (size_t)(ip[0] & 0x0fu) * 4u;
    if ((ip[0] >> 4u) != 4u || ip_header_size < RIBON_IPV4_HEADER_SIZE ||
        frame_size < RIBON_ETHERNET_HEADER_SIZE + ip_header_size +
            RIBON_UDP_HEADER_SIZE || ip[9] != 17u ||
        (load_be16(ip + 6u) & 0x3fffu) != 0u ||
        ipv4_checksum(ip, ip_header_size) != 0u ||
        memcmp(ip + 12u, context->binding->server_ipv4, 4u) != 0 ||
        memcmp(ip + 16u, context->binding->station_ipv4, 4u) != 0) {
        return RIBON_RECOVERY_NETWORK_STATUS_UNSUPPORTED;
    }
    ip_size = load_be16(ip + 2u);
    if (ip_size < ip_header_size + RIBON_UDP_HEADER_SIZE ||
        ip_size > frame_size - RIBON_ETHERNET_HEADER_SIZE) {
        return RIBON_RECOVERY_NETWORK_STATUS_MALFORMED;
    }
    udp = ip + ip_header_size;
    udp_size = load_be16(udp + 4u);
    if (load_be16(udp + 2u) != client_port ||
        (server_port != 0u && load_be16(udp) != server_port) ||
        udp_size < RIBON_UDP_HEADER_SIZE ||
        udp_size != ip_size - ip_header_size) {
        return RIBON_RECOVERY_NETWORK_STATUS_UNSUPPORTED;
    }
    *actual_server_port = load_be16(udp);
    *payload = udp + RIBON_UDP_HEADER_SIZE;
    *payload_size = udp_size - RIBON_UDP_HEADER_SIZE;
    return RIBON_RECOVERY_NETWORK_STATUS_OK;
}

/** @brief Decimal block size를 allocation 없이 RRQ option에 추가한다. */
static size_t append_decimal(uint8_t *output, uint16_t value)
{
    uint8_t reverse[5];
    size_t count = 0u;
    size_t index;
    do {
        reverse[count++] = (uint8_t)('0' + value % 10u);
        value = (uint16_t)(value / 10u);
    } while (value != 0u);
    for (index = 0u; index < count; ++index) {
        output[index] = reverse[count - index - 1u];
    }
    return count;
}

/** @brief Product path와 fixed mode/option을 TFTP RRQ payload로 내린다. */
static int build_rrq(
    const struct RibonRecoveryNetworkRequest *request,
    uint8_t *output,
    size_t capacity,
    size_t *output_size)
{
    static const char mode[] = "octet";
    static const char option[] = "blksize";
    size_t path_size = strlen(request->path);
    size_t cursor = 0u;
    if (path_size == 0u ||
        path_size > RIBON_RECOVERY_NETWORK_PATH_CAPACITY ||
        capacity < 2u + path_size + 1u + sizeof(mode) + sizeof(option) + 6u) {
        return RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT;
    }
    store_be16(output, 1u);
    cursor = 2u;
    memcpy(output + cursor, request->path, path_size + 1u);
    cursor += path_size + 1u;
    memcpy(output + cursor, mode, sizeof(mode));
    cursor += sizeof(mode);
    memcpy(output + cursor, option, sizeof(option));
    cursor += sizeof(option);
    cursor += append_decimal(output + cursor, request->block_size);
    output[cursor++] = 0u;
    *output_size = cursor;
    return RIBON_RECOVERY_NETWORK_STATUS_OK;
}

/** @brief TFTP ACK packet을 locked server transfer port로 보낸다. */
static int transmit_tftp_ack(
    struct RibonUefiRecoveryNetworkContext *context,
    uint16_t client_port,
    uint16_t server_port,
    uint16_t block,
    uint32_t *remaining_ticks)
{
    uint8_t ack[4];
    store_be16(ack, 4u);
    store_be16(ack + 2u, block);
    return transmit_udp(context, client_port, server_port,
        ack, sizeof(ack), remaining_ticks);
}

/** @brief Minimal SNP stack으로 bounded TFTP object 하나를 읽는다. */
static int snp_fetch_once(
    struct RibonUefiRecoveryNetworkContext *context,
    const struct RibonRecoveryNetworkRequest *request,
    struct RibonRecoveryNetworkResult *result)
{
    struct RibonTftpGuard guard;
    uint8_t rrq[128];
    size_t rrq_size = 0u;
    uint32_t remaining_ticks = request->deadline_ms;
    uint16_t client_port = (uint16_t)(40000u + (uint16_t)request->object_kind);
    uint16_t server_port = 0u;
    int status;
    status = resolve_server_mac(context, &remaining_ticks);
    if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return status;
    }
    status = build_rrq(request, rrq, sizeof(rrq), &rrq_size);
    if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return status;
    }
    if (ribon_tftp_guard_initialize(&guard, request->block_size,
            request->maximum_bytes) != RIBON_TFTP_GUARD_STATUS_OK) {
        return RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT;
    }
    status = transmit_udp(context, client_port, 69u,
        rrq, rrq_size, &remaining_ticks);
    if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
        return status;
    }
    for (;;) {
        const uint8_t *packet = NULL;
        const uint8_t *payload = NULL;
        size_t frame_size;
        size_t packet_size = 0u;
        size_t payload_size = 0u;
        uint16_t actual_server_port = 0u;
        uint16_t opcode;
        uint16_t block;
        uint64_t destination_offset = guard.received_bytes;
        int guard_status;
        status = snp_receive(context, &frame_size, &remaining_ticks);
        if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
            return status;
        }
        status = parse_udp(context, frame_size, client_port, server_port,
            &packet, &packet_size, &actual_server_port);
        if (status == RIBON_RECOVERY_NETWORK_STATUS_UNSUPPORTED) {
            continue;
        }
        if (status != RIBON_RECOVERY_NETWORK_STATUS_OK || packet_size < 2u) {
            return status == RIBON_RECOVERY_NETWORK_STATUS_OK ?
                RIBON_RECOVERY_NETWORK_STATUS_MALFORMED : status;
        }
        if (server_port == 0u) {
            if (actual_server_port == 0u || actual_server_port == client_port) {
                return RIBON_RECOVERY_NETWORK_STATUS_MALFORMED;
            }
            server_port = actual_server_port;
        }
        opcode = load_be16(packet);
        block = packet_size >= 4u ? load_be16(packet + 2u) : 0u;
        guard_status = ribon_tftp_guard_accept(
            &guard, packet, packet_size, &payload, &payload_size);
        if (guard_status == RIBON_TFTP_GUARD_STATUS_DUPLICATE) {
            status = transmit_tftp_ack(context, client_port, server_port,
                block, &remaining_ticks);
            if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (guard_status != RIBON_TFTP_GUARD_STATUS_OK &&
            guard_status != RIBON_TFTP_GUARD_STATUS_COMPLETE) {
            return guard_status == RIBON_TFTP_GUARD_STATUS_CAPACITY ?
                RIBON_RECOVERY_NETWORK_STATUS_CAPACITY :
                RIBON_RECOVERY_NETWORK_STATUS_MALFORMED;
        }
        if (opcode == 6u) {
            status = transmit_tftp_ack(context, client_port, server_port,
                0u, &remaining_ticks);
        } else if (opcode == 3u) {
            if (destination_offset > request->buffer_capacity ||
                payload_size > request->buffer_capacity - destination_offset) {
                return RIBON_RECOVERY_NETWORK_STATUS_CAPACITY;
            }
            memcpy((uint8_t *)request->buffer + (size_t)destination_offset,
                payload, payload_size);
            status = transmit_tftp_ack(context, client_port, server_port,
                block, &remaining_ticks);
        } else {
            return RIBON_RECOVERY_NETWORK_STATUS_MALFORMED;
        }
        if (status != RIBON_RECOVERY_NETWORK_STATUS_OK) {
            return status;
        }
        if (guard_status == RIBON_TFTP_GUARD_STATUS_COMPLETE) {
            if (guard.received_bytes == 0u) {
                return RIBON_RECOVERY_NETWORK_STATUS_TRUNCATED;
            }
            result->bytes_received = guard.received_bytes;
            return RIBON_RECOVERY_NETWORK_STATUS_OK;
        }
    }
}

/** @brief UEFI PXE Base Code에서 product path 하나를 exact-size로 읽는다. */
static int pxe_fetch_once(
    struct RibonUefiRecoveryNetworkContext *context,
    const struct RibonRecoveryNetworkRequest *request,
    struct RibonRecoveryNetworkResult *result)
{
    EFI_IP_ADDRESS server;
    UINT64 probed_size = 0u;
    UINT64 read_size;
    UINTN block_size;
    EFI_STATUS status;
    int size_known = 0;
    memset(&server, 0, sizeof(server));
    memcpy(server.v4.Addr, request->server_ipv4, sizeof(request->server_ipv4));
    block_size = request->block_size;
    status = context->pxe->Mtftp(context->pxe,
        EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE, NULL, FALSE, &probed_size,
        &block_size, &server, (UINT8 *)(uintptr_t)request->path, NULL, FALSE);
    if (status == EFI_SUCCESS) {
        if (probed_size == 0u || probed_size > request->maximum_bytes ||
            probed_size > request->buffer_capacity) {
            return RIBON_RECOVERY_NETWORK_STATUS_CAPACITY;
        }
        size_known = 1;
        read_size = probed_size;
    } else if (status == EFI_UNSUPPORTED) {
        read_size = request->maximum_bytes;
    } else {
        return fetch_status(status);
    }
    block_size = request->block_size;
    status = context->pxe->Mtftp(context->pxe,
        EFI_PXE_BASE_CODE_TFTP_READ_FILE, request->buffer, FALSE, &read_size,
        &block_size, &server, (UINT8 *)(uintptr_t)request->path, NULL, FALSE);
    if (EFI_ERROR(status)) {
        return fetch_status(status);
    }
    if (read_size == 0u || read_size > request->maximum_bytes ||
        read_size > request->buffer_capacity ||
        (size_known && read_size != probed_size)) {
        return RIBON_RECOVERY_NETWORK_STATUS_TRUNCATED;
    }
    result->bytes_received = read_size;
    return RIBON_RECOVERY_NETWORK_STATUS_OK;
}

/** @brief Product-selected backend에 one bounded fetch를 dispatch한다. */
static int recovery_fetch_once(
    void *opaque,
    const struct RibonRecoveryNetworkRequest *request,
    struct RibonRecoveryNetworkResult *result)
{
    struct RibonUefiRecoveryNetworkContext *context = opaque;
    if (context == NULL || request == NULL || result == NULL ||
        !context->opened || context->binding == NULL ||
        request->size != sizeof(*request) ||
        request->abi_version != RIBON_RECOVERY_NETWORK_ABI_VERSION ||
        result->size != sizeof(*result) ||
        result->abi_version != RIBON_RECOVERY_NETWORK_ABI_VERSION ||
        request->object_kind != result->object_kind || request->path == NULL ||
        request->buffer == NULL || request->maximum_bytes == 0u ||
        request->maximum_bytes > request->buffer_capacity ||
        request->deadline_ms == 0u || request->deadline_ms > 30000u ||
        request->block_size < 512u || request->block_size > 1468u) {
        return RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT;
    }
    if (context->backend ==
            RIBON_UEFI_RECOVERY_NETWORK_BACKEND_PXE_BASE_CODE) {
        if (request->deadline_ms <
                2u * RIBON_UEFI_PXE_OPERATION_DEADLINE_MS) {
            return RIBON_RECOVERY_NETWORK_STATUS_INVALID_ARGUMENT;
        }
        return pxe_fetch_once(context, request, result);
    }
    if (context->backend ==
            RIBON_UEFI_RECOVERY_NETWORK_BACKEND_SIMPLE_NETWORK) {
        return snp_fetch_once(context, request, result);
    }
    return RIBON_RECOVERY_NETWORK_STATUS_UNSUPPORTED;
}

static const struct RibonNetworkTransportServiceOperations network_operations = {
    .size = sizeof(network_operations),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .context = &network_context,
    .fetch = recovery_fetch_once,
};

const struct RibonServiceDescriptor ribon_uefi_bounded_tftp_network_service_descriptor = {
    .magic = RIBON_SERVICE_DESCRIPTOR_MAGIC,
    .size = sizeof(struct RibonServiceDescriptor),
    .abi_version = RIBON_SERVICE_ABI_VERSION,
    .kind = RIBON_SERVICE_KIND_NETWORK_TRANSPORT,
    .cardinality = RIBON_SERVICE_CARDINALITY_AUTHORITY,
    .lifetime = RIBON_SERVICE_LIFETIME_QUIESCE,
    .phase = RIBON_PLUGIN_PHASE_BOOT,
    .id = "service.uefi-recovery.bounded-tftp",
    .provides = RIBON_CAP_NETWORK_TRANSPORT,
    .architecture_mask = RIBON_ARCH_MASK_ALL,
    .environment_mask = RIBON_ENV_MASK_UEFI,
    .personality_mask = 0u,
    .mode_mask = RIBON_MODE_MASK(RIBON_MODE_RECOVERY) |
        RIBON_MODE_MASK(RIBON_MODE_PROVISIONING),
    .arena_budget = 4096u,
    .input_budget = RIBON_RECOVERY_NETWORK_PATH_CAPACITY,
    .output_budget = RIBON_UEFI_NETWORK_OUTPUT_BUDGET,
    .deadline_ms = 30000u,
    .operations = &network_operations,
    .operations_size = sizeof(network_operations),
    .operations_abi = RIBON_SERVICE_ABI_VERSION,
    .validate_operations = ribon_network_transport_service_operations_are_valid,
};

/** @brief PXE Base Code를 우선하고 absent일 때 exact SNP로 fail-closed fallback한다. */
int ribon_uefi_recovery_network_open(
    EFI_BOOT_SERVICES *boot_services,
    const struct RibonRecoveryNetworkProductBinding *binding)
{
    EFI_PXE_BASE_CODE_PROTOCOL *pxe_protocol = NULL;
    int pxe_discovery_status;
    int status;
    memset(&network_context, 0, sizeof(network_context));
    if (boot_services == NULL ||
        ribon_recovery_network_binding_validate(binding) !=
            RIBON_RECOVERY_NETWORK_STATUS_OK ||
        binding->transport_class !=
            RIBON_RECOVERY_NETWORK_TRANSPORT_UEFI_BOUNDED_TFTP ||
        strcmp(binding->service_id,
            ribon_uefi_bounded_tftp_network_service_descriptor.id) != 0) {
        return RIBON_UEFI_RECOVERY_NETWORK_BAD_ARGUMENT;
    }
    network_context.boot_services = boot_services;
    network_context.binding = binding;
    status = locate_one_ipv4_pxe(boot_services, &pxe_protocol);
    pxe_discovery_status = status;
    if (status == RIBON_UEFI_RECOVERY_NETWORK_OK) {
        network_context.pxe = pxe_protocol;
        status = configure_pxe(&network_context, binding);
        if (status == RIBON_UEFI_RECOVERY_NETWORK_OK) {
            network_context.backend =
                RIBON_UEFI_RECOVERY_NETWORK_BACKEND_PXE_BASE_CODE;
        }
    } else if (status == RIBON_UEFI_RECOVERY_NETWORK_NOT_FOUND ||
            status == RIBON_UEFI_RECOVERY_NETWORK_AMBIGUOUS) {
        EFI_SIMPLE_NETWORK_PROTOCOL *snp_protocol = NULL;
        status = locate_one_snp_link(boot_services, &snp_protocol);
        if (status == RIBON_UEFI_RECOVERY_NETWORK_OK) {
            network_context.snp = snp_protocol;
            status = configure_snp(&network_context);
            if (status == RIBON_UEFI_RECOVERY_NETWORK_OK) {
                network_context.backend =
                    RIBON_UEFI_RECOVERY_NETWORK_BACKEND_SIMPLE_NETWORK;
            }
        } else if (status == RIBON_UEFI_RECOVERY_NETWORK_NOT_FOUND &&
                pxe_discovery_status == RIBON_UEFI_RECOVERY_NETWORK_AMBIGUOUS) {
            status = RIBON_UEFI_RECOVERY_NETWORK_AMBIGUOUS;
        }
    }
    if (status != RIBON_UEFI_RECOVERY_NETWORK_OK) {
        memset(&network_context, 0, sizeof(network_context));
        return status;
    }
    network_context.opened = 1u;
    return RIBON_UEFI_RECOVERY_NETWORK_OK;
}

/** @brief Open 후 firmware capability probe가 선택한 backend을 반환한다. */
enum RibonUefiRecoveryNetworkBackend ribon_uefi_recovery_network_backend(void)
{
    return network_context.opened ? network_context.backend :
        RIBON_UEFI_RECOVERY_NETWORK_BACKEND_INVALID;
}
