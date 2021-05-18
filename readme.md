# Tic-Tac-Toe Server

### Original program written by Dave Ogle and Jeremy Gage, heavily modified and extended by Aaron Dickman

Final project for my Spring 2021 Network Programming class.

This repository contains the server-half of a client-server pair. An example of a client that functions with this server can be found [here](https://github.com/smebellis/network-programming-testing)

A net-enabled version of Tic-Tac-Toe that utilizes a client-server architecture.

Clients wishing to play a game with this server should note [this protocol](https://docs.google.com/document/d/1E5XI42jO6iDmBcSroVjviawFnzupJ8s6WlhOGb2VGCQ/edit?usp=sharing). Notable differences from previous version:
* Server is now capable of receiving multicasts from clients whose connection with their previous server dropped and are seeking to resume a game.
* * MC Port: 1818, IP: 239.0.0.0
* Server will extend offers to multicasting clients via a unicast which contains its protocol version number and NBO port for the client to initiate a TCP connection on.
* Server can instantiate a board state and resume a game after a client passes this data to it.
  

Other details:
* The server will refuse games from clients not using the current protocol version. **(0x06)**
* The server assumes it is always player 1.
* The server keeps tracks of its own game state, and expects the client to do likewise.

Build using:

`$ make tictactoeServer`

Run using:

`$ tictactoeServer <server-port-number> <1>`
