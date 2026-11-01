<!--
  File: third_party/opengl/README.md
  Purpose: Provenance of the vendored OpenGL registry used to generate the shim's OpenGL entry points
  Author: Akilesh
  Date: 2026-11-01
-->

# OpenGL registry

`gl.xml` is the Khronos OpenGL API registry from the OpenGL-Registry repository, commit
`6af574a14089ccfee87efe230ebcdd8742859813` (`xml/gl.xml`, committed 2026-10-06). It is licensed under
Apache-2.0 and is used unchanged by `tools/gen-gl-thunks.py` to generate the shim's OpenGL entry points.

- Source: https://github.com/KhronosGroup/OpenGL-Registry/blob/6af574a14089ccfee87efe230ebcdd8742859813/xml/gl.xml
- SHA-256: `b9ca2cfa5c676e901c20d34af3407f1687cde0f1336a5ff7a8974d04c7494ad3`

To move to a newer registry, replace the file, update this record and run `tools/build-shim.sh`.
