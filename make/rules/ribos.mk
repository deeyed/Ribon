check-ribos-parser-snapshot:
	$(PYTHON) language/ribos/host/pegen/check_parser_snapshot.py

check-ribos-parser-pilot: check-ribos-parser-snapshot $(RIBOS_PARSER_PILOT)
	$(PYTHON) language/ribos/frontend/tests/parser_pilot_tests.py \
		--parser $(RIBOS_PARSER_PILOT)
	$(RIBOS_PARSER_PILOT) \
		language/ribos/examples/executable/minimal_recovery.rbs

check-ribos-semantics: check-ribos-parser-snapshot $(RIBOS_PARSER_PILOT)
	$(PYTHON) language/ribos/frontend/tests/semantic_tests.py \
		--parser $(RIBOS_PARSER_PILOT)

$(RIBOS_SCHEMA_TEST): language/ribos/schema/tests/schema_tests.c \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/schema/tests/schema_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-schema: $(RIBOS_SCHEMA_TEST)
	$(RIBOS_SCHEMA_TEST)

$(RIBOS_IR_MODULE_TEST): language/ribos/ir/tests/module_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/ir/tests/module_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

$(RIBOS_IR_RESOURCE_TEST): language/ribos/ir/tests/resource_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/ir/tests/resource_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

check-ribos-ir: check-ribos-parser-snapshot $(RIBOS_PARSER_PILOT) \
		$(RIBOS_IR_MODULE_TEST)
	$(RIBOS_IR_MODULE_TEST)
	$(PYTHON) language/ribos/ir/tests/ir_tests.py \
		--compiler $(RIBOS_PARSER_PILOT)

check-ribos-resources: check-ribos-parser-snapshot \
		$(RIBOS_PARSER_PILOT) $(RIBOS_IR_RESOURCE_TEST)
	$(RIBOS_IR_RESOURCE_TEST)
	$(PYTHON) language/ribos/ir/tests/resource_tests.py \
		--compiler $(RIBOS_PARSER_PILOT)

$(RIBOS_ARTIFACT_TEST): language/ribos/artifact/tests/artifact_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/artifact/tests/artifact_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

check-ribos-artifact: check-ribos-parser-snapshot \
		$(RIBOS_PARSER_PILOT) $(RIBOS_ARTIFACT_TEST) \
		tools/inspect_ribos_trust_message.py \
		tests/fixtures/security/ribos-policy-trust-v1.json
	$(RIBOS_ARTIFACT_TEST)
	$(PYTHON) language/ribos/artifact/tests/artifact_tests.py \
		--compiler $(RIBOS_PARSER_PILOT)
	$(PYTHON) language/ribos/artifact/tests/trust_message_tests.py \
		--c-codec $(RIBOS_ARTIFACT_TEST) \
		--inspector tools/inspect_ribos_trust_message.py \
		--vector tests/fixtures/security/ribos-policy-trust-v1.json

$(RIBOS_VERIFIER): language/ribos/host/tools/verify.c \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/host/tools/verify.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

ribos-verify: $(RIBOS_VERIFIER)

$(RIBOS_OBJECT_DIR)/tools/run.o: language/ribos/host/tools/run.c \
		$(RIBOS_HEADERS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_RUNNER): $(RIBOS_OBJECT_DIR)/tools/run.o \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) \
		$(RIBOS_OBJECT_DIR)/tools/run.o \
		$(RIBOS_TARGET_CORE_LIB) -o $@

ribos-run: $(RIBOS_RUNNER)

check-ribos-host-boundary: ribos-libraries $(RIBOS_ALLOCATOR_TEST)
	$(PYTHON) language/ribos/host/tests/check_boundary.py \
		--target-archive $(RIBOS_TARGET_CORE_LIB) \
		--host-compiler-archive $(RIBOS_HOST_COMPILER_LIB)
	$(RIBOS_ALLOCATOR_TEST)

$(RIBOS_ALLOCATOR_TEST): language/ribos/host/tests/allocator_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/host/tests/allocator_tests.c \
		$(RIBOS_HOST_COMPILER_LIB) $(RIBOS_TARGET_CORE_LIB) \
		$(RIBOS_HOST_SUPPORT_LIB) -o $@

check-ribos-verifier: check-ribos-artifact $(RIBOS_VERIFIER)
	$(PYTHON) language/ribos/vm/tests/verifier_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER)

$(RIBOS_RUNTIME_CONTRACT_TEST): \
		language/ribos/vm/tests/runtime_contract_tests.c \
		language/ribos/vm/include/ribos/vm/runtime.h \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/vm/tests/runtime_contract_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-runtime-contract: $(RIBOS_RUNTIME_CONTRACT_TEST)
	$(PYTHON) language/ribos/vm/tests/check_runtime_header.py
	$(RIBOS_RUNTIME_CONTRACT_TEST)

$(RIBOS_PREPARED_PROGRAM_TEST): \
		language/ribos/vm/tests/prepared_program_tests.c \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/vm/tests/prepared_program_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-prepared-program: check-ribos-verifier \
		$(RIBOS_PREPARED_PROGRAM_TEST)
	$(PYTHON) language/ribos/vm/tests/check_no_raw_execute.py
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/policy.rba" \
			language/ribos/frontend/tests/semantic/positive/result_match.rbs; \
		$(RIBOS_PREPARED_PROGRAM_TEST) "$$tmp/policy.rba"

$(RIBOS_RUNTIME_STORAGE_TEST): \
		language/ribos/vm/tests/runtime_storage_tests.c \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/vm/tests/runtime_storage_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-runtime-storage: check-ribos-prepared-program \
		$(RIBOS_RUNTIME_STORAGE_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/runtime-storage.rba" \
			language/ribos/vm/tests/runtime_storage.rbs; \
		$(RIBOS_RUNTIME_STORAGE_TEST) "$$tmp/runtime-storage.rba"

$(RIBOS_VM_SCALAR_TEST): \
		language/ribos/vm/tests/scalar_interpreter_tests.c \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		-Ilanguage/ribos/vm/src/runtime \
		language/ribos/vm/tests/scalar_interpreter_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-vm-scalar: check-ribos-runtime-storage \
		$(RIBOS_VM_SCALAR_TEST)
	$(PYTHON) language/ribos/vm/tests/check_interpreter_boundary.py
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/scalar-interpreter.rba" \
			language/ribos/vm/tests/scalar_interpreter.rbs; \
		$(RIBOS_VM_SCALAR_TEST) "$$tmp/scalar-interpreter.rba"

$(RIBOS_VM_CALLS_LOOPS_TEST): \
		language/ribos/vm/tests/calls_loops_interpreter_tests.c \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		-Ilanguage/ribos/vm/src/runtime \
		language/ribos/vm/tests/calls_loops_interpreter_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-vm-calls: check-ribos-vm-scalar \
		$(RIBOS_VM_CALLS_LOOPS_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/calls-loops-interpreter.rba" \
			language/ribos/vm/tests/calls_loops_interpreter.rbs; \
		$(RIBOS_VM_CALLS_LOOPS_TEST) \
			"$$tmp/calls-loops-interpreter.rba" calls

check-ribos-vm-loops: check-ribos-vm-scalar \
		$(RIBOS_VM_CALLS_LOOPS_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/calls-loops-interpreter.rba" \
			language/ribos/vm/tests/calls_loops_interpreter.rbs; \
		$(RIBOS_VM_CALLS_LOOPS_TEST) \
			"$$tmp/calls-loops-interpreter.rba" loops

check-ribos-vm-aggregates: check-ribos-vm-calls \
		$(RIBOS_VM_CALLS_LOOPS_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/aggregate-interpreter.rba" \
			language/ribos/vm/tests/aggregate_interpreter.rbs; \
		$(RIBOS_VM_CALLS_LOOPS_TEST) \
			"$$tmp/aggregate-interpreter.rba" aggregates; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/aggregate-calls.rba" \
			language/ribos/frontend/tests/semantic/positive/aggregate_lowering.rbs; \
		$(RIBOS_VM_CALLS_LOOPS_TEST) \
			"$$tmp/aggregate-calls.rba" aggregate-calls

$(RIBOS_VM_HANDLES_TEST): \
		language/ribos/vm/tests/handle_runtime_tests.c \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		language/ribos/vm/tests/handle_runtime_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-vm-handles: check-ribos-vm-aggregates \
		$(RIBOS_VM_HANDLES_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/handle-runtime.rba" \
			language/ribos/frontend/tests/semantic/positive/result_match.rbs; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/aggregate-ownership.rba" \
			language/ribos/vm/tests/aggregate_ownership.rbs; \
		$(RIBOS_VM_HANDLES_TEST) \
			"$$tmp/handle-runtime.rba" \
			"$$tmp/aggregate-ownership.rba"

check-ribos-vm-helpers: check-ribos-vm-handles
	@echo "RIBOS-VM-HELPERS-OK dispatch=stable-id signature=typed \
budgets=bounded handles=generation-checked evidence=host-fake"

$(RIBOS_VM_TERMINAL_TEST): \
		language/ribos/vm/tests/terminal_runtime_tests.c \
		$(RIBOS_TARGET_CORE_LIB) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNFLAGS) $(RIBOS_INCLUDE_FLAGS) \
		-Ilanguage/ribos/vm/src/runtime \
		language/ribos/vm/tests/terminal_runtime_tests.c \
		$(RIBOS_TARGET_CORE_LIB) -o $@

check-ribos-vm-terminal: check-ribos-vm-helpers \
		$(RIBOS_VM_TERMINAL_TEST)
	@tmp=$$(mktemp -d); \
		trap 'rm -rf "$$tmp"' EXIT; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/action.rba" \
			language/ribos/vm/tests/runtime_storage.rbs; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/error.rba" \
			language/ribos/vm/tests/terminal_policy_error.rbs; \
		$(RIBOS_PARSER_PILOT) --emit-artifact \
			"$$tmp/journal.rba" \
			language/ribos/vm/tests/terminal_journal.rbs; \
		$(RIBOS_VM_TERMINAL_TEST) \
			"$$tmp/action.rba" "$$tmp/error.rba" \
			"$$tmp/journal.rba"

check-ribos-vm-faults: check-ribos-vm-terminal
	@echo "RIBOS-VM-FAULT-CLOSURE-OK receipt=fixed-size \
recovery=once authority=revoked rollback-claim=none evidence=host-unit"

check-ribos-host-tools: check-ribos-parser-snapshot \
		$(RIBOS_PARSER_PILOT) $(RIBOS_VERIFIER) $(RIBOS_RUNNER)
	$(RIBOS_PARSER_PILOT) --help
	$(RIBOS_VERIFIER) --help
	$(RIBOS_RUNNER) --help
	@echo "RIBOS-HOST-TOOLS-OK compiler=ribosc verifier=ribos-verify \
runner=ribos-run vm-core=shared evidence=host-build"

check-ribos-replay: check-ribos-vm-terminal check-ribos-host-tools
	$(PYTHON) language/ribos/host/tests/replay_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER) \
		--runner $(RIBOS_RUNNER)

check-ribos-conformance: check-ribos-replay
	$(PYTHON) language/ribos/host/tests/conformance_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER) \
		--runner $(RIBOS_RUNNER)

check-ribos-hostile: check-ribos-conformance
	$(PYTHON) language/ribos/host/tests/hostile_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER) \
		--runner $(RIBOS_RUNNER)

check-ribos-executable-corpus: check-ribos-parser-pilot \
		check-ribos-semantics check-ribos-vm-terminal check-ribos-host-tools
	$(PYTHON) language/ribos/examples/tests/executable_corpus_tests.py \
		--compiler $(RIBOS_PARSER_PILOT) \
		--verifier $(RIBOS_VERIFIER) \
		--runner $(RIBOS_RUNNER)

check-ribos-vm: check-ribos-vm-faults check-ribos-hostile \
		check-ribos-executable-corpus
	@echo "RIBOS-VM-R16-AGGREGATE-OK core=production \
replay=deterministic conformance=24-opcodes hostile=bounded \
executable-examples=6 evidence=host-only"

# Generation is intentionally explicit. Normal builds compile and validate the
# tracked snapshot without importing or invoking Pegen.
ribos-parser-generate:
	@test -n "$(RIBOS_PEGEN_ROOT)" || \
		{ echo "RIBOS_PEGEN_ROOT must name the pinned CPython Pegen root"; exit 2; }
	$(PYTHON) language/ribos/host/pegen/generate_parser.py \
		--pegen-root $(RIBOS_PEGEN_ROOT)

ribos-parser-regenerate-check:
	@test -n "$(RIBOS_PEGEN_ROOT)" || \
		{ echo "RIBOS_PEGEN_ROOT must name the pinned CPython Pegen root"; exit 2; }
	$(PYTHON) language/ribos/host/pegen/generate_parser.py \
		--pegen-root $(RIBOS_PEGEN_ROOT) --check

$(BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) -c $< -o $@

$(TEST_BUILD_DIR)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) -c $< -o $@

$(GENERATED_REGISTRY_C): $(HOST_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $(HOST_MANIFEST) \
		--architecture $(RIBON_ARCH) \
		--output $@ \
		--report $(GENERATED_REGISTRY_REPORT)

$(GENERATED_REGISTRY_O): $(GENERATED_REGISTRY_C)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(CORE_LIB): $(CORE_OBJS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(CORE_OBJS)

$(BOOT_LIB): $(BOOT_LIB_OBJS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(BOOT_LIB_OBJS)

$(SDK_LIB): $(SDK_LIB_OBJS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(SDK_LIB_OBJS)

$(SDK_UPDATE_LIB): $(SDK_UPDATE_OBJS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $(SDK_UPDATE_OBJS)

$(HOST_REFERENCE): \
	$(HOST_MAIN_OBJ) $(ARCH_OBJS) $(HOST_PRODUCT_OBJS) \
	$(GENERATED_REGISTRY_O) $(RIBOS_POLICY_LIB) \
	$(BOOT_LIB) $(CORE_LIB) $(RIBOS_TARGET_CORE_LIB) $(HOST_SECURITY_OBJS)
	$(CC) $(CFLAGS) $(WARNFLAGS) $^ -o $@

$(RIBOS_RIBON_INTEGRATION_TEST): \
		tests/policy/ribos_integration_tests.c \
		$(ARCH_OBJS) $(HOST_PRODUCT_OBJS) $(GENERATED_REGISTRY_O) \
		$(RIBOS_POLICY_LIB) $(BOOT_LIB) $(CORE_LIB) \
		$(RIBOS_TARGET_CORE_LIB) $(HOST_SECURITY_OBJS) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) \
		tests/policy/ribos_integration_tests.c \
		$(ARCH_OBJS) $(HOST_PRODUCT_OBJS) $(GENERATED_REGISTRY_O) \
		$(RIBOS_POLICY_LIB) $(BOOT_LIB) $(CORE_LIB) \
		$(RIBOS_TARGET_CORE_LIB) $(HOST_SECURITY_OBJS) -o $@

$(RIBOS_HOST_UNSIGNED): $(RIBOS_PARSER_PILOT) \
		language/ribos/vm/tests/aggregate_ownership.rbs
	@mkdir -p $(@D)
	$(RIBOS_PARSER_PILOT) --emit-artifact $@ \
		language/ribos/vm/tests/aggregate_ownership.rbs

$(RIBOS_HOST_SIGNED_A): $(RIBOS_HOST_UNSIGNED) $(HOST_MANIFEST) \
		tests/fixtures/security/rfc8032-test1-seed.hex tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py --input $< --output $@ \
		--product-manifest $(HOST_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-host-reference-key \
		--rollback-domain ribon.policy.host-reference.v1 \
		--sequence 1 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a

$(RIBOS_HOST_SIGNED_B): $(RIBOS_HOST_UNSIGNED) $(HOST_MANIFEST) \
		tests/fixtures/security/rfc8032-test1-seed.hex tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py --input $< --output $@ \
		--product-manifest $(HOST_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-host-reference-key \
		--rollback-domain ribon.policy.host-reference.v1 \
		--sequence 2 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a

$(RIBOS_HOST_WRONG_KEY): $(RIBOS_HOST_UNSIGNED) $(HOST_MANIFEST) \
		tests/fixtures/security/rfc8032-test1-seed.hex tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py --input $< --output $@ \
		--product-manifest $(HOST_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-host-unknown-key \
		--rollback-domain ribon.policy.host-reference.v1 \
		--sequence 1 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a

$(RIBOS_HOST_VERIFIER_INVALID_UNSIGNED): $(RIBOS_HOST_UNSIGNED) \
		tools/make_ribos_verifier_invalid.py
	$(PYTHON) tools/make_ribos_verifier_invalid.py --input $< --output $@

$(RIBOS_HOST_VERIFIER_INVALID): $(RIBOS_HOST_VERIFIER_INVALID_UNSIGNED) \
		$(HOST_MANIFEST) tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py --input $< --output $@ \
		--product-manifest $(HOST_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-host-reference-key \
		--rollback-domain ribon.policy.host-reference.v1 \
		--sequence 2 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a

check-ribos-ribon-integration: $(RIBOS_RIBON_INTEGRATION_TEST) \
		$(RIBOS_HOST_UNSIGNED) $(RIBOS_HOST_SIGNED_A) \
		$(RIBOS_HOST_SIGNED_B) $(RIBOS_HOST_WRONG_KEY) \
		$(RIBOS_HOST_VERIFIER_INVALID)
	$(RIBOS_RIBON_INTEGRATION_TEST) $(RIBOS_HOST_UNSIGNED) \
		$(RIBOS_HOST_SIGNED_A) $(RIBOS_HOST_SIGNED_B) \
		$(RIBOS_HOST_WRONG_KEY) $(RIBOS_HOST_VERIFIER_INVALID)

check-ribos-product-graphs:
	$(PYTHON) tools/lint/ribos_product_graph_lint.py \
		--manifest $(HOST_MANIFEST) --architecture $(RIBON_ARCH)

check-ribos-normal-no-network:
	$(PYTHON) tools/lint/ribos_normal_no_network_lint.py \
		--manifest $(HOST_MANIFEST) --architecture $(RIBON_ARCH)

check-ribos-factory-recovery: check-ribos-ribon-integration
	@echo "RIBOS-FACTORY-RECOVERY-OK external-artifact=optional \
authorization=fail-closed notification=once evidence=host-object"

$(RIBOS_R18_MANIFEST): $(HOST_MANIFEST) tools/make_ribos_qemu_manifest.py
	$(PYTHON) tools/make_ribos_qemu_manifest.py \
		--input $(HOST_MANIFEST) --output $@

$(RIBOS_R18_UNSIGNED_A): $(RIBOS_PARSER_PILOT) \
		language/ribos/vm/tests/aggregate_ownership.rbs
	@mkdir -p $(@D)
	$(RIBOS_PARSER_PILOT) --emit-artifact $@ \
		language/ribos/vm/tests/aggregate_ownership.rbs

$(RIBOS_R18_UNSIGNED_B): $(RIBOS_PARSER_PILOT) \
		language/ribos/vm/tests/aggregate_ownership.rbs
	@mkdir -p $(@D)
	$(RIBOS_PARSER_PILOT) --emit-artifact $@ \
		language/ribos/vm/tests/aggregate_ownership.rbs

$(RIBOS_R18_ARTIFACT): $(RIBOS_R18_UNSIGNED_A) \
		$(RIBOS_R18_MANIFEST) $(RIBOS_R18_GOLDEN_SHA256) \
		tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py \
		--input $< --output $@ --product-manifest $(RIBOS_R18_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-validation-policy-key \
		--rollback-domain ribon.policy.ribos-qemu-validation.v1 \
		--sequence 18 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a \
		--expected-sha256 $(RIBOS_R18_GOLDEN_SHA256)

$(RIBOS_R18_ARTIFACT_B): $(RIBOS_R18_UNSIGNED_B) \
		$(RIBOS_R18_MANIFEST) $(RIBOS_R18_GOLDEN_SHA256) \
		tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py \
		--input $< --output $@ --product-manifest $(RIBOS_R18_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-validation-policy-key \
		--rollback-domain ribon.policy.ribos-qemu-validation.v1 \
		--sequence 18 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a \
		--expected-sha256 $(RIBOS_R18_GOLDEN_SHA256)

$(RIBOS_R18_TRIAL_ARTIFACT): $(RIBOS_R18_UNSIGNED_A) \
		$(RIBOS_R18_MANIFEST) $(RIBOS_R18_TRIAL_GOLDEN_SHA256) \
		tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py \
		--input $< --output $@ --product-manifest $(RIBOS_R18_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-validation-policy-key \
		--rollback-domain ribon.policy.ribos-qemu-validation.v1 \
		--sequence 19 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a \
		--expected-sha256 $(RIBOS_R18_TRIAL_GOLDEN_SHA256)

$(RIBOS_R18_TRIAL_ARTIFACT_B): $(RIBOS_R18_UNSIGNED_B) \
		$(RIBOS_R18_MANIFEST) $(RIBOS_R18_TRIAL_GOLDEN_SHA256) \
		tests/fixtures/security/rfc8032-test1-seed.hex \
		tools/sign_ribos_policy.py
	$(PYTHON) tools/sign_ribos_policy.py \
		--input $< --output $@ --product-manifest $(RIBOS_R18_MANIFEST) \
		--private-seed tests/fixtures/security/rfc8032-test1-seed.hex \
		--key-id ribon-validation-policy-key \
		--rollback-domain ribon.policy.ribos-qemu-validation.v1 \
		--sequence 19 --mode normal \
		--expected-public-key \
			d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a \
		--expected-sha256 $(RIBOS_R18_TRIAL_GOLDEN_SHA256)

$(RIBOS_R18_EMBED_C): $(RIBOS_R18_ARTIFACT) tools/embed_binary.py
	$(PYTHON) tools/embed_binary.py --input $< --output $@ \
		--symbol ribon_ribos_validation_artifact

$(RIBOS_R18_TRIAL_EMBED_C): \
		$(RIBOS_R18_TRIAL_ARTIFACT) tools/embed_binary.py
	$(PYTHON) tools/embed_binary.py --input $< --output $@ \
		--symbol ribon_ribos_validation_trial_artifact

check-ribos-golden-artifact: \
		$(RIBOS_R18_ARTIFACT) $(RIBOS_R18_ARTIFACT_B) \
		$(RIBOS_R18_TRIAL_ARTIFACT) $(RIBOS_R18_TRIAL_ARTIFACT_B)
	cmp $(RIBOS_R18_ARTIFACT) $(RIBOS_R18_ARTIFACT_B)
	cmp $(RIBOS_R18_TRIAL_ARTIFACT) $(RIBOS_R18_TRIAL_ARTIFACT_B)
	@echo "RIBOS-R18-GOLDEN-ARTIFACT-OK policies=2 rebuilds=2 \
wire=little-endian"

$(RIBOS_R18_AARCH64_REGISTRY_C): \
		$(RIBOS_R18_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $< --architecture aarch64 --output $@ \
		--report $(RIBOS_R18_AARCH64_GRAPH)

$(RIBOS_R18_AARCH64_DIR)/obj/%.o: %.c $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) $(SECURITY_INCLUDE_FLAGS) \
		-ffunction-sections -fdata-sections -c $< -o $@

$(RIBOS_R18_AARCH64_DIR)/obj/generated/plugin_registry.o: \
		$(RIBOS_R18_AARCH64_REGISTRY_C) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_R18_AARCH64_DIR)/obj/generated/embedded_policy.o: \
		$(RIBOS_R18_EMBED_C) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_AARCH64_DIR)/obj/generated/embedded_trial_policy.o: \
		$(RIBOS_R18_TRIAL_EMBED_C) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(AARCH64_CC) $(AARCH64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_AARCH64_DIR)/obj/targets/qemu-aarch64-virt-raw-fdt/entry.o: \
		targets/qemu-aarch64-virt-raw-fdt/entry.S
	@mkdir -p $(@D)
	$(AARCH64_CC) --target=aarch64-none-elf -c $< -o $@

$(RIBOS_R18_AARCH64_VM_LIB): $(RIBOS_R18_AARCH64_VM_OBJS) $(RIBON_MAKEFILES)
	$(RM) $@
	$(LLVM_AR) rcs $@ $(RIBOS_R18_AARCH64_VM_OBJS)

$(RIBOS_R18_AARCH64_ELF): $(RIBOS_R18_AARCH64_OBJS) \
		$(RIBOS_R18_AARCH64_VM_LIB) \
		targets/qemu-aarch64-virt-raw-fdt/linker.ld
	$(LD_LLD) -m aarch64elf --gc-sections \
		-T targets/qemu-aarch64-virt-raw-fdt/linker.ld \
		-Map=$(RIBOS_R18_AARCH64_DIR)/ribon-ribos.map \
		-o $@ $(RIBOS_R18_AARCH64_OBJS) $(RIBOS_R18_AARCH64_VM_LIB)

$(RIBOS_R18_AARCH64_IMAGE): $(RIBOS_R18_AARCH64_ELF)
	$(OBJCOPY) -O binary $< $@

$(RIBOS_R18_RISCV64_REGISTRY_C): \
		$(RIBOS_R18_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $< --architecture riscv64 --output $@ \
		--report $(RIBOS_R18_RISCV64_GRAPH)

$(RIBOS_R18_RISCV64_DIR)/obj/%.o: %.c $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) $(SECURITY_INCLUDE_FLAGS) \
		-ffunction-sections -fdata-sections -c $< -o $@

$(RIBOS_R18_RISCV64_DIR)/obj/generated/plugin_registry.o: \
		$(RIBOS_R18_RISCV64_REGISTRY_C) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_R18_RISCV64_DIR)/obj/generated/embedded_policy.o: \
		$(RIBOS_R18_EMBED_C) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_RISCV64_DIR)/obj/generated/embedded_trial_policy.o: \
		$(RIBOS_R18_TRIAL_EMBED_C) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(RISCV64_CC) $(RISCV64_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_RISCV64_DIR)/obj/targets/qemu-riscv64-virt-opensbi/entry.o: \
		targets/qemu-riscv64-virt-opensbi/entry.S
	@mkdir -p $(@D)
	$(RISCV64_CC) --target=riscv64-none-elf -march=rv64gc \
		-mabi=lp64d -mcmodel=medany -c $< -o $@

$(RIBOS_R18_RISCV64_VM_LIB): $(RIBOS_R18_RISCV64_VM_OBJS) $(RIBON_MAKEFILES)
	$(RM) $@
	$(LLVM_AR) rcs $@ $(RIBOS_R18_RISCV64_VM_OBJS)

$(RIBOS_R18_RISCV64_ELF): $(RIBOS_R18_RISCV64_OBJS) \
		$(RIBOS_R18_RISCV64_VM_LIB) \
		targets/qemu-riscv64-virt-opensbi/linker.ld
	$(LD_LLD) -m elf64lriscv --gc-sections \
		-T targets/qemu-riscv64-virt-opensbi/linker.ld \
		-Map=$(RIBOS_R18_RISCV64_DIR)/ribon-ribos.map \
		-o $@ $(RIBOS_R18_RISCV64_OBJS) $(RIBOS_R18_RISCV64_VM_LIB)

$(RIBOS_R18_RISCV64_IMAGE): $(RIBOS_R18_RISCV64_ELF)
	$(OBJCOPY) -O binary $< $@

$(RIBOS_R18_AMD64_REGISTRY_C): \
		$(RIBOS_R18_MANIFEST) tools/generate_plugin_registry.py
	$(PYTHON) tools/generate_plugin_registry.py \
		--manifest $< --architecture x86_64 --output $@ \
		--report $(RIBOS_R18_AMD64_GRAPH)

$(RIBOS_R18_AMD64_DIR)/obj/%.o: %.c $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) $(SECURITY_INCLUDE_FLAGS) \
		-ffunction-sections -fdata-sections -c $< -o $@

$(RIBOS_R18_AMD64_DIR)/obj/generated/plugin_registry.o: \
		$(RIBOS_R18_AMD64_REGISTRY_C) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) \
		$(RIBOS_TARGET_INCLUDE_FLAGS) -c $< -o $@

$(RIBOS_R18_AMD64_DIR)/obj/generated/embedded_policy.o: \
		$(RIBOS_R18_EMBED_C) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_AMD64_DIR)/obj/generated/embedded_trial_policy.o: \
		$(RIBOS_R18_TRIAL_EMBED_C) $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(X86_64_CC) $(UEFI_FLAGS) $(DEPFLAGS) -c $< -o $@

$(RIBOS_R18_AMD64_VM_LIB): $(RIBOS_R18_AMD64_VM_OBJS) $(RIBON_MAKEFILES)
	$(RM) $@
	$(LLVM_AR) rcs $@ $(RIBOS_R18_AMD64_VM_OBJS)

$(RIBOS_R18_AMD64_APP): $(RIBOS_R18_AMD64_OBJS) \
		$(RIBOS_R18_AMD64_VM_LIB)
	$(LLD_LINK) /subsystem:efi_application /entry:efi_main /nodefaultlib \
		/brepro /opt:ref \
		/machine:x64 /map:$(RIBOS_R18_AMD64_DIR)/ribon-ribos.map \
		/out:$@ $(RIBOS_R18_AMD64_OBJS) $(RIBOS_R18_AMD64_VM_LIB)

$(RIBOS_R18_AMD64_ESP)/EFI/BOOT/BOOTX64.EFI: $(RIBOS_R18_AMD64_APP)
	@mkdir -p $(@D)
	cp $< $@

check-ribos-cross-arch-objects: \
		$(RIBOS_R18_AMD64_APP) \
		$(RIBOS_R18_AARCH64_IMAGE) \
		$(RIBOS_R18_RISCV64_IMAGE)
	$(PYTHON) tools/lint/ribos_cross_arch_object_lint.py \
		--map $(RIBOS_R18_AMD64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_AARCH64_DIR)/ribon-ribos.map \
		--map $(RIBOS_R18_RISCV64_DIR)/ribon-ribos.map \
		--image $(RIBOS_R18_AMD64_APP) \
		--image $(RIBOS_R18_AARCH64_ELF) \
		--image $(RIBOS_R18_RISCV64_ELF)

$(SECURITY_TEST): tests/security/ed25519_provider_tests.c \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/security/signature.h \
		include/Ribon/security/ed25519.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(SECURITY_INCLUDE_FLAGS) \
		tests/security/ed25519_provider_tests.c \
		$(SECURITY_PROVIDER_SRCS) -o $@

$(SECURITY_SANITIZER_TEST): tests/security/ed25519_provider_tests.c \
		$(SECURITY_PROVIDER_SRCS) include/Ribon/security/signature.h \
		include/Ribon/security/ed25519.h $(RIBON_MAKEFILES)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g $(WARNFLAGS) \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SECURITY_INCLUDE_FLAGS) tests/security/ed25519_provider_tests.c \
		$(SECURITY_PROVIDER_SRCS) -o $@
