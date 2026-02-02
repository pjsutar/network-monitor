# Live Transport Network Monitor — System Design Document

---

## 1. Executive Summary

The **Live Transport Network Monitor (LTNM)** is a real-time C++17 system that ingests passenger tap-in/tap-out events from a metropolitan underground network and serves "quiet route" recommendations to reduce peak-hour overcrowding. The system connects to an upstream event feed over **WebSocket/TLS + STOMP 1.2**, maintains an in-memory directed graph of the transport network, and exposes a STOMP server for downstream clients requesting optimal routes.

**Key metrics (design targets):**

| Metric | Target |
|---|---|
| Passenger event throughput | 300 events/sec (≤ 3.3 ms/event) |
| Quiet-route query latency | 6 req/sec (≤ 150 ms/request) |
| Concurrent client connections | Bounded by OS file descriptors |

**Technical highlights:**

- **Single-threaded async I/O** via Boost.ASIO strands — zero mutex contention.
- **Modified Yen's k-shortest-paths** combined with real-time crowding data for route selection.
- **Policy-based (template) design** — every network layer is compile-time injectable, enabling deterministic unit testing with zero virtual-dispatch overhead.
- **Zero-copy STOMP parsing** — `std::string_view` headers and body avoid allocation on the hot path.

---

## 2. System Overview

### Problem Statement

Underground networks experience severe overcrowding during peak hours. Passengers have no visibility into which alternative routes are less congested, leading to uneven load across lines. The system must (a) track real-time crowding at every station, and (b) recommend the quietest route between any two stations, subject to an acceptable travel-time increase.

### Goals

1. **Low-latency event ingestion** — keep the in-memory graph current within one second of real-world events.
2. **Fast quiet-route computation** — return a crowding-optimized route within 150 ms.
3. **Testability** — every component must be unit-testable in isolation, without live network dependencies.
4. **Observability** — built-in profiling infrastructure (compile-time toggleable) for performance regression detection.

### High-Level Approach

The system is a **single-process, event-driven pipeline**: an upstream STOMP client subscribes to passenger events, updates a weighted directed graph, and a downstream STOMP server fields route queries against that graph. All I/O is asynchronous and multiplexed on one thread via `boost::asio::io_context`.

---

## 3. Architecture

### Component Diagram

```
 ┌─────────────────────────────────────────────────────────────────────┐
 │                        NetworkMonitor (Orchestrator)                │
 │                        network-monitor.h:89-552                    │
 │                                                                     │
 │  ┌──────────────┐    ┌───────────────────┐    ┌──────────────────┐ │
 │  │ StompClient   │───▶│  TransportNetwork │◀───│  StompServer     │ │
 │  │ stomp-client.h│    │  transport-       │    │  stomp-server.h  │ │
 │  │ :56-700       │    │  network.h:173-501│    │  :56-748         │ │
 │  └──────┬────────┘    └───────────────────┘    └──────┬───────────┘ │
 │         │                                              │            │
 │  ┌──────┴────────┐                            ┌───────┴──────────┐ │
 │  │ WebSocket     │                            │ WebSocket        │ │
 │  │ Client        │                            │ Server           │ │
 │  │ ws-client.h   │                            │ ws-server.h      │ │
 │  │ :28-329       │                            │ :28-527          │ │
 │  └──────┬────────┘                            └───────┬──────────┘ │
 │         │              boost::asio::io_context         │            │
 └─────────┼──────────────────────────────────────────────┼────────────┘
           │  TLS/WebSocket                     TLS/WebSocket
           ▼                                              ▲
   ┌───────────────┐                            ┌─────────┴────────┐
   │ Upstream Event │                            │ Downstream       │
   │ Feed Server    │                            │ Route Clients    │
   └───────────────┘                            └──────────────────┘
```

### Technology Stack

| Layer | Technology |
|---|---|
| Language | C++17 (structured bindings, `std::optional`, `std::filesystem`) |
| Async I/O | Boost.ASIO 1.74+ / Boost.Beast (WebSocket) |
| TLS | OpenSSL (TLSv1.2, client + server contexts) |
| Messaging | STOMP 1.2 over WebSocket |
| JSON | nlohmann/json |
| HTTP downloads | libcurl (certificate-verified HTTPS) |
| Logging | spdlog |
| Build | CMake 3.17+ with Conan package management |
| Testing | Boost.Test + custom mocks |

### Design Patterns

| Pattern | Where | Purpose |
|---|---|---|
| Policy-based design (templates) | All network classes | Compile-time DI; mock injection for testing |
| Async completion tokens | WebSocket/STOMP layers | Non-blocking callbacks through the I/O chain |
| Strand serialization | StompClient, StompServer | Logical thread safety without mutexes |
| Observer / Pub-Sub | STOMP subscriptions | Decouple event producers from consumers |
| Flyweight (string_view) | StompFrame parsing | Zero-copy header/body access |
| RAII | Timer, SSL contexts, connections | Deterministic resource cleanup |

---

## 4. Detailed Component Design

### 4.1 TransportNetwork (`transport-network.h:173-501`, `transport-network.cpp:1-1015`)

**Responsibility:** Owns the in-memory graph of the transport network. Answers pathfinding queries and records live passenger events.

**Internal representation:**

```
GraphNode (station)               GraphEdge (connection)
┌──────────────────────┐         ┌──────────────────────┐
│ id: Id                │────────▶│ route: RouteInternal* │
│ name: string          │  edges  │ nextStop: GraphNode*  │
│ passengerCount: int64 │         │ travelTime: uint      │
└──────────────────────┘         └──────────────────────┘
```

Stations are `GraphNode`s stored in `std::unordered_map<Id, shared_ptr<GraphNode>>`. Each node holds a vector of outgoing `GraphEdge`s, forming a directed adjacency list. Lines and routes are stored separately for O(1) lookup by ID (`transport-network.h:408-420`).

**Key interfaces:**

- `FromJson(network, lines)` — Builds the graph from two JSON files (station/line topology). Validates referential integrity: every route stop must reference an existing station (`transport-network.cpp:97-213`).
- `RecordPassengerEvent(event)` — Atomically increments or decrements `passengerCount` on the target station node (`transport-network.cpp:267-288`). This is the hot path at 300 events/sec.
- `GetQuietTravelRoute(stationA, stationB, maxSlowdownPc, minQuietnessPc, maxNPaths)` — The primary query interface. Internally calls `GetFastestTravelRoutes` (Yen's algorithm) to enumerate candidate paths, then selects the one with minimum total crowding within the quietness threshold (`transport-network.cpp:520-630`).

**Why this design:** A directed adjacency list with `shared_ptr` nodes enables O(1) station lookup and O(degree) neighbor traversal, matching the access patterns of Dijkstra's algorithm. The `passengerCount` field is updated in-place (no separate data structure) to ensure the pathfinder always reads current crowding data without synchronization — possible because the entire system is single-threaded.

### 4.2 StompClient (`stomp-client.h:56-700`)

**Responsibility:** Maintains a persistent STOMP 1.2 connection to the upstream event feed. Manages subscriptions and dispatches received messages to registered callbacks.

**Template parameter:** `WsClient` — any type satisfying the WebSocket client concept (Connect, Send, Close, callbacks).

**State machine:**

```
Disconnected → Connecting → Connected → Subscribed
     ↑              │            │           │
     └──────────────┴────────────┴───────────┘
                    (on error)
```

On connection, the client sends a `STOMP` frame with `accept-version:1.2` and login credentials (`stomp-client.h:380-409`). Upon receiving `CONNECTED`, it transitions to the connected state and triggers the user's `onConnect` callback. Subscriptions are tracked in `std::unordered_map<std::string, Subscription>` keyed by subscription ID (`stomp-client.h:356`).

**Strand architecture:** The client operates on its own `boost::asio::strand` (`stomp-client.h:326`), separate from the underlying WebSocket stream. All user-facing callbacks are posted to this strand, preventing re-entrant calls into the WebSocket layer.

### 4.3 StompServer (`stomp-server.h:56-748`)

**Responsibility:** Accepts downstream client connections over WebSocket/TLS and speaks STOMP 1.2 server-side. Routes `SEND` frames from clients to the `NetworkMonitor` for processing.

**Connection tracking:** Uses a bidirectional map — `connections_` maps `Session → Connection` (status, subscriptions), while `sessions_` maps `session-id string → Session` (`stomp-server.h:407-412`). This enables efficient lookup in both directions: by session pointer (on message receipt) and by session ID (for targeted message delivery).

**Session lifecycle:**

1. WebSocket accept → status = `kPending`
2. Receive STOMP `CONNECT` frame → validate, send `CONNECTED`, status = `kConnected`
3. Receive `SUBSCRIBE` → register subscription destination
4. Receive `SEND` → dispatch to `NetworkMonitor` handler
5. Receive `DISCONNECT` or connection drop → cleanup session

### 4.4 StompFrame (`stomp-frame.h:111-206`, `stomp-frame.cpp:355-531`)

**Responsibility:** Parses, validates, and constructs STOMP 1.2 frames.

**Zero-copy parsing:** The raw frame string is stored in `plain_` (`stomp-frame.h:114`). Headers and body are `std::string_view` references into `plain_`, avoiding allocation during the parse phase. The parser extracts the command (first line), iterates colon-delimited headers (first occurrence wins per STOMP 1.2 spec), locates the blank-line separator, and reads the body until the NULL terminator — or exactly `content-length` bytes if that header is present (`stomp-frame.cpp:427-453`).

**Trade-off:** Because headers are views into `plain_`, the copy constructor must re-parse the frame from the copied string (`stomp-frame.cpp:291-295`). This is acceptable because frames are rarely copied on the hot path.

### 4.5 WebSocket Client & Server (`websocket-client.h`, `websocket-server.h`)

**Responsibility:** Thin async wrappers around Boost.Beast WebSocket streams with TLS.

The **client** chains: DNS resolve → TCP connect → TLS handshake (with SNI, `websocket-client.h:220`) → WebSocket handshake → recursive `async_read` loop. The **server** uses a recursive `AcceptConnection` pattern (`websocket-server.h:472-496`): accept a connection, spawn a `WebSocketSession`, immediately accept the next. Each session is a `shared_from_this`-enabled object whose lifetime extends through all pending async operations.

### 4.6 NetworkMonitor Orchestrator (`network-monitor.h:89-552`)

**Responsibility:** Wires all components together. Owns the `io_context`, both SSL contexts, the `StompClient`, `StompServer`, and `TransportNetwork`.

`Configure()` (`network-monitor.h:105-236`) downloads the network layout JSON via `FileDownloader`, constructs the `TransportNetwork` from JSON, creates the STOMP client (connecting to the upstream feed), and creates the STOMP server (listening for route queries). `Run()` simply calls `ioc_.run()`, entering the event loop.

---

## 5. Network Protocols

### STOMP 1.2 over WebSocket over TLS

**Why STOMP over raw WebSocket?** STOMP provides a lightweight pub-sub semantic (destinations, subscriptions, message framing) without the complexity of AMQP or MQTT. It runs naturally over WebSocket, giving us bidirectional messaging with standard HTTP upgrade, firewall-friendly on port 443.

**Why WebSocket over plain TCP?** The upstream event feed is an external service that speaks WebSocket. Additionally, WebSocket gives us automatic framing (no need for custom length-prefix protocol), wide proxy/load-balancer compatibility, and a natural upgrade path from HTTPS.

**Why TLS 1.2?** All connections — both upstream (client) and downstream (server) — are TLS-encrypted. The client context performs full certificate verification (`network-monitor.h:339`). The server context uses a separate certificate for downstream clients (`network-monitor.h:341`). TLSv1.2 was chosen as the minimum secure version at the time of development.

**Frame wire format** (STOMP 1.2):

```
COMMAND\n
header-key:header-value\n
...\n
\n
body-bytes\0
```

The parser handles both content-length-delimited and NULL-terminated bodies. Header validation is command-specific: e.g., `SUBSCRIBE` requires `destination` and `id`; `SEND` requires `destination` (`stomp-frame.cpp:469-531`).

---

## 6. Data Flow

### Passenger Event Ingestion (Hot Path)

```
1. Upstream server pushes a STOMP MESSAGE frame over WebSocket/TLS
   containing JSON: {"station_id": "X", "type": "in", "timestamp": "..."}

2. WebSocketClient::OnRead() receives the raw bytes
   → websocket-client.h:285-310

3. StompClient::OnWsMessage() parses the STOMP frame
   → stomp-client.h:472-520
   → StompFrame constructor: O(n) parse, zero allocation

4. StompClient::HandleSubscriptionMessage() looks up the subscription
   by destination, invokes the registered callback
   → stomp-client.h:546-580

5. NetworkMonitor::OnNetworkEventsMessage() deserializes JSON into
   a PassengerEvent struct
   → network-monitor.h:435-460

6. TransportNetwork::RecordPassengerEvent() finds the station's
   GraphNode in O(1) via hash map, increments/decrements passengerCount
   → transport-network.cpp:267-288

Total path: WebSocket read → STOMP parse → JSON parse → hash lookup → counter update
Target budget: ≤ 3.3 ms
```

### Quiet Route Query (Query Path)

```
1. Client sends STOMP SEND frame to destination "/quiet-route"
   Body: {"start_station_id": "A", "end_station_id": "B"}

2. StompServer receives, parses, dispatches to NetworkMonitor
   → stomp-server.h:596-640

3. NetworkMonitor::OnQuietRouteClientMessage() parses JSON request
   → network-monitor.h:496-510

4. TransportNetwork::GetQuietTravelRoute() executes:
   a. Dijkstra from A to B → fastest path (baseline time T)
      → transport-network.cpp:767-882
   b. Yen's algorithm → up to maxNPaths paths within T * (1 + maxSlowdownPc)
      → transport-network.cpp:884-1004
   c. For each candidate path, sum passengerCount at all stops
      → transport-network.cpp:1006-1015
   d. Select path with minimum crowding within quietness threshold
      → transport-network.cpp:580-601

5. NetworkMonitor serializes TravelRoute to JSON, sends STOMP MESSAGE
   back to the requesting client's session
   → network-monitor.h:510-540

Target budget: ≤ 150 ms
```

---

## 7. Threading & Concurrency Model

### Architecture: Single-Threaded Event Loop with Strands

The entire system runs on **one OS thread** executing `ioc_.run()` (`main.cpp:49`). There are no mutexes, condition variables, or atomic operations in the codebase. Thread safety is achieved structurally through Boost.ASIO's strand mechanism.

### Strand Layout

```
┌─────────────────────────────────────────────┐
│              boost::asio::io_context         │
│                                              │
│  ┌─────────────────┐  ┌──────────────────┐  │
│  │ WS Client Strand │  │ WS Server Strand │  │
│  │ (implicit)       │  │ (implicit)       │  │
│  └────────┬─────────┘  └────────┬─────────┘  │
│           │                      │            │
│  ┌────────┴─────────┐  ┌────────┴──────────┐ │
│  │ STOMP Client      │  │ STOMP Server      │ │
│  │ Strand            │  │ Strand            │ │
│  │ stomp-client.h:326│  │ stomp-server.h:407│ │
│  └──────────────────┘  └───────────────────┘ │
└─────────────────────────────────────────────┘
```

Each STOMP layer creates its own `boost::asio::strand` wrapping the same `io_context`. The strand guarantees that handlers posted to it execute **serially** — never concurrently — even if `io_context::run()` were called from multiple threads in the future. This provides a clean upgrade path to multi-threaded execution without code changes.

### Why Single-Threaded?

1. **Simplicity** — No shared mutable state means no data races, no deadlocks, no lock contention.
2. **Cache efficiency** — All data stays on one core's cache hierarchy.
3. **Sufficient throughput** — At 300 events/sec (3.3 ms budget) and 6 queries/sec (150 ms budget), a single core is adequate. Dijkstra on a city-scale graph (~300 stations, ~1000 edges) completes in microseconds.
4. **Deterministic testing** — Single-threaded execution makes tests reproducible.

### Async Lifetime Management

Objects participating in async chains use `std::enable_shared_from_this` (`websocket-server.h:33`). When an async operation is initiated, the handler captures a `shared_ptr` to the owning object, preventing premature destruction. This is critical for `WebSocketSession`, which may have multiple pending operations (read + write) simultaneously.

### Callback Posting Pattern

User-facing callbacks are never invoked directly from within a WebSocket handler. Instead, they are posted to the STOMP strand:

```cpp
boost::asio::post(context_, [onConnect = onConnect_]() {
    onConnect(Error::kCouldNotConnectToWebSocketServer);
});
```

This prevents re-entrant calls (e.g., a user callback that calls `Send()` from within an `OnMessage` handler) from corrupting internal state.

---

## 8. Performance & Scalability

### Current Performance Characteristics

| Operation | Complexity | Expected Latency |
|---|---|---|
| Passenger event recording | O(1) hash lookup + counter increment | < 0.01 ms |
| STOMP frame parse | O(n) where n = frame size | < 0.1 ms |
| JSON parse (event) | O(n) where n = payload size | < 0.1 ms |
| Dijkstra shortest path | O((V+E) log V), V≈300, E≈1000 | < 1 ms |
| Yen's k-shortest paths | O(k·V·(V+E) log V) | < 100 ms |
| Crowding calculation | O(path length) | < 0.01 ms |

The system comfortably meets the 3.3 ms/event and 150 ms/query targets on modern hardware. The **bottleneck is Yen's algorithm** when `maxNPaths` is large or the graph is dense.

### Built-in Profiling

The compile-time `USE_TIMER` flag (`CMakeLists.txt:7`) activates RAII-based profiling macros:

```cpp
TIMER_START(GetQuietTravelRoute);    // transport-network.cpp:528
// ... computation ...
TIMER_STOP(GetQuietTravelRoute);     // transport-network.cpp:628
```

`Timer::Report()` outputs a formatted table with best/worst/average latencies per named region (`timer.cpp:136-183`), suitable for CI performance regression gates.

### Identified Bottlenecks

1. **Yen's path enumeration** — Each iteration runs a full Dijkstra. For large `maxNPaths`, this dominates query latency. Mitigation: the `maxSlowdownPc` threshold prunes the search space early (`transport-network.cpp:979-983`).

2. **Route endpoint lookup** — `GetTravelRoutes` performs O(lines × routes) linear scan to find routes ending at a given station (`transport-network.cpp:329`). A reverse index (`station → routes ending here`) would reduce this to O(1).

3. **Path copying in Yen's** — Candidate paths are assembled by copying vectors (`transport-network.cpp:959-965`). Move semantics could reduce allocation pressure.

### Scaling Approach

The single-threaded model scales **vertically** (faster CPU, larger cache). For horizontal scaling:

- **Multiple instances** behind a load balancer, each maintaining its own graph copy fed from the same event stream (shared-nothing architecture).
- **Shard by geographic region** — partition the network graph into zones, route queries to the appropriate shard.
- **Multi-threaded io_context** — The strand-based design already supports calling `ioc_.run()` from multiple threads. Read-heavy workloads (queries) could run concurrently; writes (event recording) would need a reader-writer lock on `passengerCount`.

---

## 9. Key Design Decisions

### Decision 1: Template-Based Dependency Injection over Virtual Interfaces

**Choice:** All network components (`WebSocketClient`, `StompClient`, etc.) are class templates parameterized on their dependencies.

**Rationale:** Virtual dispatch adds indirection on every call — unacceptable on the hot path (300 events/sec through multiple layers). Templates enable the compiler to inline the entire call chain from WebSocket read through STOMP parse to graph update. The cost is longer compile times and more complex error messages, but the runtime benefit is measurable. This also enables mock injection for testing without any production-code overhead (`tests/websocket-client-mock.h`).

### Decision 2: Single-Threaded Async over Multi-Threaded

**Choice:** One `io_context`, one thread, strand-based serialization.

**Rationale:** The workload is I/O-bound (waiting for events) with short CPU bursts (parsing, pathfinding). A single thread eliminates all synchronization overhead and makes the system deterministic for testing. The strand architecture preserves the option to go multi-threaded later without restructuring. Given the target throughput (300 events/sec), a single core is far from saturated.

### Decision 3: Modified Yen's Algorithm for Quiet Routes

**Choice:** Use Yen's k-shortest-paths with early termination based on travel-time threshold, then select the least crowded path.

**Rationale:** Pure Dijkstra finds the fastest route but ignores crowding. A crowding-weighted Dijkstra would conflate two different concerns (speed vs. comfort) into a single metric, making the trade-off opaque to users. Yen's algorithm enumerates multiple fast routes, and the crowding selection is a separate, interpretable step. The `maxSlowdownPc` parameter gives users explicit control over the speed-comfort trade-off.

### Decision 4: Route-Change Penalty in Pathfinding

**Choice:** A 5-minute penalty is added when a path switches between lines (`transport-network.cpp:820-826`).

**Rationale:** Changing lines in an underground network involves walking between platforms, waiting for the next train, and cognitive overhead. Without this penalty, the algorithm would recommend routes with many transfers that are technically faster in graph distance but slower in practice. The 5-minute constant is a tunable heuristic.

### Decision 5: In-Place Passenger Counting on Graph Nodes

**Choice:** `passengerCount` lives directly on `GraphNode` rather than in a separate data structure.

**Rationale:** The pathfinder reads crowding data during traversal. Co-locating it with the node avoids an extra hash lookup per edge relaxation in Dijkstra. Since the system is single-threaded, there is no read-write contention. This gives optimal cache locality — the pathfinder touches node data that is already in L1 cache from the adjacency list traversal.

### Decision 6: STOMP 1.2 over Custom Protocol

**Choice:** Use STOMP as the application-level messaging protocol over WebSocket.

**Rationale:** STOMP provides pub-sub semantics (destinations, subscriptions), message framing, and error reporting out of the box. It is human-readable (text-based), simplifying debugging with standard tools like `wscat`. The upstream event feed already speaks STOMP, so using the same protocol for downstream clients reduces protocol translation complexity. The overhead of text framing versus binary is negligible at 300 messages/sec.

---

## 10. API Reference

This section documents the public interfaces of each library component. All async methods follow the Boost.ASIO completion-token pattern: the final parameter is an optional callback receiving an error code and (where applicable) a result value.

### 10.1 NetworkMonitor — Orchestrator API

**Header:** `inc/network-monitor/network-monitor.h`

#### Configuration

```cpp
struct NetworkMonitorConfig {
    std::string networkEventsUrl{};           // Upstream STOMP server hostname
    std::string networkEventsPort{};          // Upstream port (e.g., "443")
    std::string networkEventsUsername{};       // STOMP login
    std::string networkEventsPassword{};      // STOMP passcode
    std::filesystem::path caCertFile{};       // CA certificate for TLS verification
    std::filesystem::path networkLayoutFile{};// JSON network topology file
    std::string quietRouteHostname{"localhost"};
    std::string quietRouteIp{"127.0.0.1"};
    unsigned short quietRoutePort{8042};
    double quietRouteMaxSlowdownPc{0.1};      // Max 10% slower than fastest
    double quietRouteMinQuietnessPc{0.1};     // Min 10% quieter to justify slowdown
    size_t quietRouteMaxNPaths{20};           // Yen's algorithm path limit
};
```

#### Methods

| Method | Signature | Description |
|---|---|---|
| `Configure` | `NetworkMonitorError Configure(const NetworkMonitorConfig&)` | Initializes all components; downloads network layout, builds graph, creates STOMP client/server. Does **not** start the event loop. |
| `Run` | `void Run()` | Enters the `io_context` event loop. Blocks until `Stop()` is called or all work completes. |
| `Run` | `void Run(std::chrono::duration<Rep,Ratio> runFor)` | Runs for a bounded duration, then returns. |
| `Stop` | `void Stop()` | Stops the event loop. May leave in-flight messages incomplete. |
| `GetLastErrorCode` | `NetworkMonitorError GetLastErrorCode() const` | Returns the last error that caused the event loop to exit. |
| `GetLastTravelRoute` | `TravelRoute GetLastTravelRoute() const` | Returns the most recently computed quiet route. |
| `GetNetworkRepresentation` | `const TransportNetwork& GetNetworkRepresentation() const` | Read-only access to the internal transport graph. |
| `SetNetworkCrowding` | `void SetNetworkCrowding(const std::unordered_map<Id, int>&)` | Testing helper: pre-seeds passenger counts. |

#### Error Codes (`NetworkMonitorError`)

| Code | Meaning |
|---|---|
| `kOk` | Success |
| `kCouldNotConnectToStompClient` | Upstream STOMP connection failed |
| `kCouldNotParsePassengerEvent` | Malformed event JSON |
| `kCouldNotParseQuietRouteRequest` | Malformed route query JSON |
| `kCouldNotRecordPassengerEvent` | Station not found in graph |
| `kCouldNotStartStompServer` | Downstream server bind/listen failed |
| `kCouldNotSubscribeToPassengerEvents` | STOMP SUBSCRIBE rejected |
| `kFailedNetworkLayoutFileDownload` | HTTPS download failed |
| `kFailedNetworkLayoutFileParsing` | JSON parse error on layout file |
| `kFailedTransportNetworkConstruction` | Graph construction error |
| `kMissingCaCertFile` | CA cert path does not exist |
| `kMissingNetworkLayoutFile` | Network layout path does not exist |
| `kStompClientDisconnected` | Upstream connection dropped |
| `kStompServerClientDisconnected` | A downstream client disconnected |
| `kStompServerDisconnected` | Downstream server stopped unexpectedly |

### 10.2 TransportNetwork — Graph & Pathfinding API

**Header:** `inc/network-monitor/transport-network.h`

#### Domain Types

```cpp
using Id = std::string;

struct Station     { Id id; std::string name; };
struct Route       { Id id; std::string direction; Id lineId;
                     Id startStationId; Id endStationId; std::vector<Id> stops; };
struct Line        { Id id; std::string name; std::vector<Route> routes; };

struct PassengerEvent {
    enum class Type { In, Out };
    Id stationId;  Type type;  boost::posix_time::ptime timestamp;
};

struct TravelRoute {
    struct Step { Id startStationId; Id endStationId;
                  Id lineId; Id routeId; unsigned int travelTime; };
    Id startStationId; Id endStationId;
    unsigned int totalTravelTime; std::vector<Step> steps;
};
```

#### Methods

| Method | Signature | Returns |
|---|---|---|
| `FromJson` | `bool FromJson(nlohmann::json&& src)` | `false` if travel times missing; throws on structural errors |
| `AddStation` | `bool AddStation(const Station&)` | `false` if duplicate or malformed |
| `AddLine` | `bool AddLine(const Line&)` | `false` if referenced stations missing |
| `RecordPassengerEvent` | `bool RecordPassengerEvent(const PassengerEvent&)` | `false` if station unknown |
| `GetPassengerCount` | `long long int GetPassengerCount(const Id&) const` | Current count; throws if station unknown |
| `GetRoutesServingStation` | `std::vector<Id> GetRoutesServingStation(const Id&) const` | Route IDs or empty |
| `SetTravelTime` | `bool SetTravelTime(const Id& a, const Id& b, unsigned int)` | `false` if stations not adjacent |
| `GetTravelTime` | `unsigned int GetTravelTime(const Id& a, const Id& b) const` | Minutes; `0` if not adjacent or same |
| `GetFastestTravelRoute` | `TravelRoute GetFastestTravelRoute(const Id& a, const Id& b) const` | Dijkstra shortest path |
| `GetQuietTravelRoute` | `TravelRoute GetQuietTravelRoute(const Id& a, const Id& b, double maxSlowdownPc, double minQuietnessPc, size_t maxNPaths) const` | Least-crowded path within time budget |

#### JSON Wire Formats

**PassengerEvent** (ingested from upstream):

```json
{ "station_id": "station_042", "type": "In", "timestamp": "2025-11-15T08:30:00" }
```

**Quiet Route Request** (received from downstream clients via STOMP `SEND`):

```json
{ "start_station_id": "station_001", "end_station_id": "station_099" }
```

**Quiet Route Response** (sent back via STOMP `MESSAGE`):

```json
{
    "start_station_id": "station_001",
    "end_station_id": "station_099",
    "total_travel_time": 42,
    "steps": [
        {
            "start_station_id": "station_001",
            "end_station_id": "station_015",
            "line_id": "line_northern",
            "route_id": "route_a",
            "travel_time": 18
        },
        {
            "start_station_id": "station_015",
            "end_station_id": "station_099",
            "line_id": "line_central",
            "route_id": "route_b",
            "travel_time": 24
        }
    ]
}
```

### 10.3 STOMP Client API

**Header:** `inc/network-monitor/stomp-client.h`

| Method | Signature | Description |
|---|---|---|
| Constructor | `StompClient(url, endpoint, port, ioc, ctx)` | Creates client; does not connect. |
| `Connect` | `void Connect(username, password, onConnect, onMessage, onDisconnect)` | Initiates WebSocket + STOMP handshake. Callbacks run on a dedicated strand. |
| `Subscribe` | `std::string Subscribe(destination, onSubscribe, onMessage)` | Returns subscription ID (empty on failure). `onMessage` fires per incoming `MESSAGE` frame. |
| `Send` | `std::string Send(destination, content, onSend)` | Sends a `SEND` frame with `application/json` content type. Returns request ID. |
| `Close` | `void Close(onClose)` | Graceful STOMP `DISCONNECT` + WebSocket close. |

### 10.4 STOMP Server API

**Header:** `inc/network-monitor/stomp-server.h`

| Method | Signature | Description |
|---|---|---|
| Constructor | `StompServer(host, ip, port, ioc, ctx)` | Creates server; does not listen. |
| `Run` | `StompServerError Run(onClientConnect, onClientMessage, onClientDisconnect, onDisconnect)` | Starts accepting connections. Returns `kOk` on success. |
| `Send` | `std::string Send(connectionId, destination, content, onSend, userRequestId)` | Sends a `MESSAGE` frame to a specific client. Returns request ID. |
| `Close` | `void Close(connectionId, onClientClose)` | Closes a specific client connection. |
| `Stop` | `void Stop()` | Stops accepting new connections; closes all existing sessions. |

### 10.5 StompFrame — Protocol Parsing API

**Header:** `inc/network-monitor/stomp-frame.h`

#### Construction

```cpp
// Parse from raw string
StompFrame(StompError& ec, std::string&& frame);

// Build from components
StompFrame(StompError& ec, StompCommand command,
           const std::unordered_map<StompHeader, std::string>& headers,
           const std::string& body);
```

#### Accessors

| Method | Returns |
|---|---|
| `GetCommand()` | `StompCommand` — one of 15 STOMP commands |
| `HasHeader(StompHeader)` | `bool` |
| `GetHeaderValue(StompHeader)` | `std::string_view` into the original frame buffer (zero-copy) |
| `GetBody()` | `std::string_view` into the original frame buffer |
| `ToString()` | Full serialized frame as `std::string` |

### 10.6 WebSocket Client & Server API

**Headers:** `inc/network-monitor/websocket-client.h`, `inc/network-monitor/websocket-server.h`

**Client** (`WebSocketClient<Resolver, WebSocketStream>`):

| Method | Signature |
|---|---|
| `Connect` | `void Connect(onConnect, onMessage, onDisconnect)` |
| `Send` | `void Send(const std::string& message, onSend)` |
| `Close` | `void Close(onClose)` |

**Server** (`WebSocketServer<Acceptor, WebSocketStream>`):

| Method | Signature |
|---|---|
| `Run` | `error_code Run(onSessionConnect, onSessionMessage, onSessionDisconnect, onDisconnect)` |
| `Stop` | `void Stop()` |

**Session** (`WebSocketSession<WebSocketStream>`):

| Method | Signature |
|---|---|
| `Send` | `void Send(const std::string& message, onSend)` |
| `Close` | `void Close(onClose)` |

Concrete type aliases for production use:

```cpp
using BoostWebSocketClient = WebSocketClient<tcp::resolver, beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>>;
using BoostWebSocketServer = WebSocketServer<tcp::acceptor, beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>>;
```

### 10.7 Utility APIs

**FileDownloader** (`inc/network-monitor/file-downloader.h`):

```cpp
bool DownloadFile(const std::string& fileUrl,
                  const std::filesystem::path& destination,
                  const std::filesystem::path& caCertFile = {});

nlohmann::json ParseJsonFile(const std::filesystem::path& source);
```

**Timer** (`inc/network-monitor/timer.h`):

```cpp
Timer(const std::string& name);     // RAII: starts on construction
void Stop() noexcept;               // Stops measurement
static TimerResults GetResults(const std::string& name);
static void PrintReport();          // Logs formatted table of all timers
static void ClearAll();
```

**Environment** (`inc/network-monitor/env.h`):

```cpp
std::string GetEnvVar(const std::string& envVar,
                      const std::optional<std::string>& defaultValue = std::nullopt);
// Throws std::runtime_error if variable not found and no default provided.
```

---

*Document generated from codebase analysis. All file references are relative to the repository root.*
