<!--
  File: third_party/vulkan/README.md
  Purpose: Provenance of the vendored Vulkan registry used to generate the shim's Vulkan entry points
  Author: Akilesh
  Date: 2026-10-29
-->

# Vulkan registry

`vk.xml` is the Khronos Vulkan API registry from the Vulkan-Headers repository, tag `v1.3.290`
(`registry/vk.xml`). It is licensed under Apache-2.0 and is used unchanged by
`tools/gen-vulkan-thunks.py` to generate the shim's Vulkan entry points.

- Source: https://github.com/KhronosGroup/Vulkan-Headers/blob/v1.3.290/registry/vk.xml
- SHA-256: `4e63ee09a641f311e7d666e1f5d61a40acdff5d84d9ebffaa4a0b14c9abcc427`

To move to a newer Vulkan version, replace the file, update this record and run `tools/build-shim.sh`.
