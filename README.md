# Distributed Link-State Routing Protocol

A distributed link-state routing system implemented using **C++ and Python**. The project consists of an **Oracle Node (ON)** and multiple **Virtual Nodes (VNs)**. The ON initializes the VNs with topology information, while the VNs exchange and flood Link-State Advertisements (LSAs) to reconstruct the complete network topology.

## Overview

The system works as follows:

1. The **Oracle Node** reads the network topology from a configuration file.
2. Virtual Nodes connect to the Oracle Node using **TCP**.
3. The Oracle Node assigns node IDs and provides each VN with its neighbors, link costs, and UDP endpoints.
4. Each VN generates its local link-state information and sends it to neighboring VNs using **UDP**.
5. VNs flood received LSAs through their neighbors, allowing every node to reconstruct the global topology.
6. **Sequence numbers** are used to discard duplicate and stale LSAs and prevent repeated propagation.
7. The Oracle Node monitors the topology configuration and can propagate **dynamic topology changes** to the VNs.
8. Link-state information is also periodically re-flooded to keep topology information synchronized.

## Architecture

```text
                         topology.txt
                              |
                              v
                     +----------------+
                     |  Oracle Node   |
                     |      (ON)      |
                     +-------+--------+
                             |
                         TCP |
                             |
          +------------------+------------------+
          |                  |                  |
          v                  v                  v
      +-------+          +-------+          +-------+
      |  VN 0 | <------> |  VN 1 | <------> |  VN 2 |
      +-------+    UDP   +-------+    UDP   +-------+
          ^                  ^                  ^
          |                  |                  |
          +------------------+------------------+
                     LSA Flooding
```

### Communication

| Connection | Protocol | Purpose |
|---|---|---|
| Oracle Node → Virtual Nodes | TCP | Initialization and topology updates |
| Virtual Node ↔ Virtual Node | UDP | LSA exchange and flooding |
| LSA identification | Sequence Number | Duplicate/stale packet suppression |

## Project Structure

```text
.
├── on.py
├── vn.cpp
├── topology.txt
└── README.md
```

- `on.py` — Oracle Node implementation
- `vn.cpp` — Virtual Node implementation
- `topology.txt` — Network topology configuration
- `README.md` — Project documentation

## Requirements

- Linux / Unix-like operating system
- Python 3
- C++ compiler supporting C++11 or later
- POSIX socket support

## How to Run

### 1. Compile the Virtual Node

```bash
g++ -std=c++11 vn.cpp -o vn
```

### 2. Start the Oracle Node

The Oracle Node takes the topology configuration file as its argument:

```bash
python3 on.py topology.txt
```

The Oracle Node starts its TCP server and waits for Virtual Nodes to connect.

### 3. Start the Virtual Nodes

Open a separate terminal for each Virtual Node.

```bash
./vn <ON_PORT> <ON_IP> <VN_UDP_PORT> <VN_IP>
```

For example, when running everything on `localhost`:

```bash
./vn 5000 127.0.0.1 6001 127.0.0.1
./vn 5000 127.0.0.1 6002 127.0.0.1
./vn 5000 127.0.0.1 6003 127.0.0.1
```

Each VN must use a **unique UDP port**.

### Complete Startup Example

Use four terminals:

**Terminal 1 — Oracle Node**

```bash
python3 on.py topology.txt
```

**Terminal 2 — VN 0**

```bash
./vn 5000 127.0.0.1 6001 127.0.0.1
```

**Terminal 3 — VN 1**

```bash
./vn 5000 127.0.0.1 6002 127.0.0.1
```

**Terminal 4 — VN 2**

```bash
./vn 5000 127.0.0.1 6003 127.0.0.1
```

## Topology Initialization

The Oracle Node reads the topology from `topology.txt` and uses it to determine the links and costs between nodes.

For example, a three-node topology:

```text
10 20
30
```

represents:

```text
        10
   VN 0 ---- VN 1
     \        /
      \20    /30
       \    /
        VN 2
```

where:

- `VN 0 ↔ VN 1` has cost `10`
- `VN 0 ↔ VN 2` has cost `20`
- `VN 1 ↔ VN 2` has cost `30`

## Link-State Advertisement Flooding

After initialization, every VN generates an LSA describing its local link-state information.

An LSA contains information about:

- Originating node
- Sequence number
- Neighbor
- Link cost

The LSA is sent to the node's neighbors using UDP.

When a VN receives an LSA:

```text
Receive LSA
    |
    v
Check sequence number
    |
    +---- Old / Duplicate ----> Discard
    |
    +---- New ---------------> Update topology
                                  |
                                  v
                            Forward to neighbors
```

This flooding mechanism allows link-state information originating at one VN to propagate throughout the network.

## Sequence Numbers

Each VN maintains sequence numbers for link-state information.

When an LSA is received, the VN compares its sequence number with the latest sequence number known for that originating node.

Older or already-seen LSAs are discarded. Newer LSAs are recorded and forwarded.

This prevents:

- Duplicate packet processing
- Stale topology information
- Repeated LSA propagation
- Flooding loops

## Dynamic Topology Updates

The Oracle Node monitors the topology configuration file while the system is running.

If the topology changes, the ON reloads the updated configuration and sends the relevant information to the connected VNs over TCP.

The VNs then generate updated link-state information and propagate it through the network using UDP flooding.

The system also performs **periodic re-flooding** of link-state information.

Therefore, topology changes can be introduced without restarting the entire system.

## Implementation Details

### Oracle Node

Implemented in **Python** using socket-based networking and `select()` for handling multiple connections.

Responsibilities:

- Read the topology configuration
- Accept VN TCP connections
- Assign node IDs
- Provide local neighbor information
- Send topology updates
- Detect configuration changes
- Trigger periodic link-state updates

### Virtual Node

Implemented in **C++** using POSIX sockets.

Responsibilities:

- Connect to the Oracle Node over TCP
- Receive node and neighbor information
- Maintain local link-state information
- Exchange LSAs with neighboring VNs over UDP
- Track sequence numbers
- Suppress duplicate/stale LSAs
- Forward new LSAs
- Reconstruct the global network topology

The VN uses `select()` to monitor its TCP and UDP sockets concurrently.

## Dynamic Update Flow

```text
                topology.txt
                     |
              file modification
                     |
                     v
               Oracle Node
                     |
                    TCP
                     |
                     v
              Virtual Nodes
                     |
                    UDP
                     |
                     v
                LSA Flooding
                     |
                     v
             Complete Network
                Topology
```

## Notes

- TCP is used for communication between the Oracle Node and Virtual Nodes.
- UDP is used for VN-to-VN LSA dissemination.
- UDP packets are not explicitly acknowledged; sequence numbers provide duplicate/stale packet suppression rather than delivery confirmation.
- The Oracle Node acts as the initial source of topology information, while the VNs distribute and maintain link-state information through flooding.
