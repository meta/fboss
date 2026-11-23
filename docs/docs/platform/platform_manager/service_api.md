---
id: service_api
title: Thrift Service API
sidebar_label: Service API
sidebar_position: 9
---

# PlatformManager Service API

PlatformManager exposes a Thrift service on port 5975 (default) that allows
querying platform state and exploration status.

The Thrift interface is defined in
[platform_manager_service.thrift](https://github.com/meta/fboss/blob/main/fboss/platform/platform_manager/platform_manager_service.thrift).

The PlatformManager service is consumed programmatically by other FBOSS services.
For example, `sensor_service` uses `PmClientFactory` to connect.

The following is an example of using a custom thrift client to query the service.

```
./pm_thrift_client getPlatformName
MONTBLANC


./pm_thrift_client getBspVersion
{
  "bspBaseName": "fboss_bsp_kmods",
  "bspVersion": "3.4.0-1",
  "kernelVersion": "6.12.49-1.el9.x86_64"
}
```
