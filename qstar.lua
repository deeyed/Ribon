qstar.project {
  name = "ribon-core",
  version = "0.1.0",
  root = ".",
  build_dir = "build/qstar",
  generated_dir = "build/qstar/generated",
  compile_commands = "build",
}

qstar.import_file("qstar/policies/toolsets.qst")

qstar.import_file("qstar/policies/configs.qst")

qstar.subdir("src/core")

qstar.subdir("src/boot")

qstar.subdir("src/arch")

qstar.subdir("src/firmware")

qstar.subdir("src/loader")

qstar.subdir("src/profiles")

qstar.subdir("tests")

qstar.group "ribon_core" {
  deps = {
    "//src/core:ribon_core_x86_64",
    "//src/core:ribon_core_aarch64",
    "//src/core:ribon_core_riscv64",
  },
}

qstar.group "ribon_host_smokes" {
  deps = {
    "//src/boot:host_smoke_x86_64",
    "//src/boot:host_smoke_aarch64",
    "//src/boot:host_smoke_riscv64",
  },
}

qstar.group "ribon_tests" {
  deps = {
    "//tests:elf64_loader_tests",
    "//tests:rph1_builder_tests",
    "//tests:x86_64_direct_high_tests",
    "//tests:aarch64_direct_high_tests",
    "//tests:arch_ops_x86_64_tests",
    "//tests:arch_ops_aarch64_tests",
    "//tests:arch_ops_riscv64_tests",
    "//tests:core_service_boundary_tests",
    "//tests:mode_descriptor_normal_tests",
    "//tests:mode_descriptor_recovery_tests",
    "//tests:mode_descriptor_provisioning_tests",
    "//tests:mode_descriptor_diagnostic_tests",
    "//tests:uefi_hardening_tests",
  },
}

qstar.group "ribon_all" {
  deps = {
    "//:ribon_core",
    "//src/core:ribon_non_normal_mode_cores",
    "//:ribon_host_smokes",
    "//:ribon_tests",
  },
}
