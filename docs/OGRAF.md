# Native OGraf v1 graphics

CasparCG supports OGraf v1 real-time graphics and classic HTML templates side by
side. HTML support is not deprecated and existing HTML commands keep their
responses and behavior.

## Graphic packages

Place an OGraf manifest ending in `.ograf.json` anywhere below `template-path`.
Its `main` entry must be a relative path that remains inside the directory
containing the manifest. Manifests are validated against the pinned OGraf v1
schema when discovered. CasparCG rejects non-real-time graphics and
`renderRequirements` that the renderer cannot satisfy.

Graphics can be selected by manifest ID or relative manifest path:

- `foo.ograf.json` selects OGraf.
- `foo.html` selects classic HTML.
- `foo` selects OGraf when only the OGraf manifest exists.
- `foo` continues to select HTML when both variants exist.

One CasparCG stage layer is one OGraf RenderTarget. A RenderTarget can contain
multiple GraphicInstances, each with a UUID. AMCP CG layers identify instances
within that RenderTarget.

## AMCP

OGraf uses the existing `CG` command family with optional JSON parameters:

```text
CG <channel[-stage-layer]> ADD <cg-layer> <graphic.ograf.json> [start-label] <play-on-load> [data-json]
CG <channel[-stage-layer]> PLAY <cg-layer> [play-params-json]
CG <channel[-stage-layer]> NEXT <cg-layer> [options-json]
CG <channel[-stage-layer]> UPDATE <cg-layer> <data-json> [options-json]
CG <channel[-stage-layer]> STOP <cg-layer> [options-json]
CG <channel[-stage-layer]> INVOKE <cg-layer> <custom-action-id> [custom-params-json]
CG <channel[-stage-layer]> REMOVE <cg-layer>
```

`PLAY` accepts `goto` or `delta` plus `skipAnimation`. `NEXT` always advances
by one step. `UPDATE`, `STOP`, and `INVOKE` accept their OGraf v1 parameters,
including `skipAnimation`. A non-empty Flash `STARTLABEL` is invalid for OGraf.

Successful OGraf actions return `201 CG OK` followed by a JSON object containing
`statusCode`, `statusMessage`, `result`, `currentStep`, and
`graphicInstanceId`. Classic HTML replies remain unchanged.

## Server API

The OGraf Server REST API is disabled by default. Configure it in
`casparcg.config`:

```xml
<ograf>
    <server>
        <enabled>false</enabled>
        <host>127.0.0.1</host>
        <port>8080</port>
        <base-path>/ograf/v1</base-path>
    </server>
    <action-timeout-ms>30000</action-timeout-ms>
    <dispose-timeout-ms>5000</dispose-timeout-ms>
    <access-to-public-internet>false</access-to-public-internet>
</ograf>
```

The process exposes one renderer named `casparcg`. RenderTargets use:

```json
{"channel": 1, "layer": 20}
```

The API implements the pinned OGraf v1 OpenAPI operations for server, graphic,
renderer, and RenderTarget information; Load, Clear, Play, Update, Stop, and
CustomAction; and graphic Delete. REST and AMCP address the same live
GraphicInstances.

Errors use RFC 7807 problem objects. The local listener returns
`Access-Control-Allow-Origin: *`. There is no authentication in this release;
binding to a non-loopback address logs a security warning and does not enable
wildcard CORS.

Delete only removes the selected manifest. It never deletes its directory or
shared assets. The manifest is first renamed atomically to a tombstone. Without
`force=true`, existing instances continue until removed. With `force=true`,
matching instances are disposed first. Orphaned tombstones are cleaned during
the next server start.

## Network access

OGraf blocks public HTTP, HTTPS, WebSocket, and other external requests by
default. Embedded resources, local files, and loopback URLs remain available.
Set `<access-to-public-internet>true</access-to-public-internet>` only for
trusted graphics that require external services. This setting does not change
classic HTML network behavior.

## Build options

- `ENABLE_WEB` controls the shared CEF renderer.
- `ENABLE_HTML` controls classic HTML templates.
- `ENABLE_OGRAF` controls native OGraf support.

The supported combinations are:

| Build | CMake options | `WEB` | `HTML` | `OGRAF` |
| --- | --- | --- | --- | --- |
| HTML and OGraf (default) | none | on | on | on |
| Classic HTML only | `-DENABLE_OGRAF=OFF` | on | on | off |
| OGraf only | `-DENABLE_HTML=OFF -DENABLE_WEB=ON` | on | off | on |
| No web rendering | `-DENABLE_WEB=OFF` | off | off | off |
| Compatible HTML-off invocation | `-DENABLE_HTML=OFF` | off | off | off |

`ENABLE_WEB=OFF` always disables both web-based graphic modules, even if a
module option is explicitly enabled. For compatibility, a new build configured
with `ENABLE_HTML=OFF` and no explicit `ENABLE_WEB` value also disables web
rendering. An explicit `ENABLE_WEB` value takes precedence over that compatibility
behavior and is therefore required for an OGraf-only build.

Only OGraf v1 real-time rendering is in scope. Non-real-time rendering, graphic
upload, marketplace integration, remote package management, and HTML
deprecation are not included.
