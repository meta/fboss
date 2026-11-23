---
oncalls: [fboss_agent]
---

## Adding SAI attributes

When implementing a new SAI attribute, follow the steps below:

1. Determine the API category. For example an attribute from `saibuffer.h` or
   `saibufferextensions.h` belongs to the Buffer API.

2. Update the following files (replace `Buffer` with the API category name):

fbcode/fboss/agent/hw/sai/api/BufferApi.h
fbcode/fboss/agent/hw/sai/api/tests/BufferApiTest.cpp
fbcode/fboss/agent/hw/sai/tracer/BufferApiTracer.cpp
fbcode/fboss/agent/hw/sai/fake/FakeSaiBuffer.h
fbcode/fboss/agent/hw/sai/fake/FakeSaiBuffer.cpp
fbcode/fboss/agent/hw/sai/store/tests/BufferStoreTest.cpp

3. If the attribute is an extension, also update these files (again, replace
   `Buffer` with the API category name):

fbcode/fboss/agent/hw/sai/api/bcm/BufferApi.cpp
fbcode/fboss/agent/hw/sai/api/oss/BufferApi.cpp
fbcode/fboss/agent/hw/sai/api/tajo/BufferApi.cpp
fbcode/fboss/agent/hw/sai/api/chenab/BufferApi.cpp
fbcode/fboss/agent/hw/sai/api/fake/FakeSaiExtensions.cpp
fbcode/fboss/agent/hw/sai/api/fake/saifakeextensions.h

4. Run the SAI store and api tests for the changed APIs, for example:
```bash
buck test fbcode//fboss/agent/hw/sai/store/tests:buffer_store_test-fake fbcode//fboss/agent/hw/sai/api/tests:buffer_api_test-fake
```

Note that VSCode does not define all the necessary compilation flags, so code
guarded in `#if SAI_API_VERSION >= ...` or `#if defined(...)` blocks may
appear to have compile errors. Please ignore these errors. In general, do not
make any changes to unrelated attributes.
