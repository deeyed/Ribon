qstar.project {
  name = "ribon",
  version = "0.2.0",
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
qstar.subdir("targets")
qstar.subdir("tests")

qstar.group "ribon_libraries" {
  deps = {
    "//src/core:libribon_core",
    "//src/common:libribon_boot",
  },
}

qstar.group "ribon_all" {
  deps = {
    "//:ribon_libraries",
    "//src/environments/host:ribon_host_products",
    "//src/image-formats:ribon_image_formats",
    "//targets:ribon_boot_targets",
    "//tests:ribon_tests",
  },
}
