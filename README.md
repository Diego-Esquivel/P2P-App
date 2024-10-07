# P2P-App

## About the App

The objective of the application is to enable peers to communicate with each other about the current state of the blockchain by sending the most up-to-date version.

A file 'blockchain.txt' stores the string represenation of the current state of the blockchain. This file is transmitted to all connected peers whenever a local update is detected or when a new version is received from another peer.

A peer connected when a host accepts its connection request. The peer sending the connection request is a connection to the host accepting the request but not the other way around. 

When a host received a connection request is creates a dedicated thread for each established connection. These threads monitor the 'blockchain.txt' & act accordingly when a change is received || observed on the local system.

This app uses the GNU Sockets API to establish a Stream based connection with peers.

## Needs to get implemented:

1. Validate thread-safety
1. Resume normal operation after broadcasting a connection has left the network.
1. Handle large 'blockchain.txt'
1. Testing framework

## Proof of operation

### Listen Mode Operation

![Listen Mode Operation Screengrab] (ListenMode.png)

### Handling Multiple Connections Operation

![Handling Multiple Connection Operation Screengrab] (HandlingMConnection.png)

### Two Connection Mode Processes Operation

![Two instances of Connection Mode processes Operation Creengrab] (TwoConnectionModeProc.png)

### Broadcasting Operation

![Message Broadcasting Operation Screengrab] (https://raw.githubusercontent.com/Diego-Esquivel/P2P-App/refs/heads/main/MsgBroadcasting.png)
