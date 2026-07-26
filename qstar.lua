qstar.project {
  name = "ribon",
  version = "0.3.0",
  root = ".",
  build_dir = "build/qstar",
  generated_dir = "build/qstar/generated",
  compile_commands = "build",
}

qstar.import_file("qstar/policies/toolsets.qst")
qstar.import_file("qstar/policies/configs.qst")

qstar.subdir("src/core")
qstar.subdir("src/common")
qstar.subdir("src/arch")
qstar.subdir("src/image-formats")
qstar.subdir("src/environments/host")
qstar.subdir("src/protocols")
qstar.subdir("src/plugins")
qstar.subdir("src/firmware")
qstar.subdir("targets")
qstar.subdir("sdk")
qstar.subdir("products/firmware")
qstar.subdir("tests")

qstar.group "ribon_libraries" {
  deps = {
    "//src/core:libribon_core",
    "//src/common:libribon_boot",
    "//src/plugins:libribon_sdk",
  },
}

qstar.group "ribon_all" {
  deps = {
    "//:ribon_libraries",
    "//src/environments/host:ribon_host_products",
    "//src/image-formats:ribon_image_formats",
    "//targets:ribon_boot_targets",
    "//sdk:ribon_sdk_products",
    "//products/firmware:ribon_firmware_provider_products",
    "//tests:ribon_tests",
  },
}
