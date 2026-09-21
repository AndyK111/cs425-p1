# Project 1

- Name: Andy Kempf
- Email: AndyKempf@u.boisestate.edu
- Class: CS425 - 001

## Known Bugs or Issues

All tests pass, and my message to the test server finds its way with no issues. Coverage report claims 100% coverage, and leak tests find no leaks

## Experience

I didn't end up with very much time, so I ddelegated most of the nuts & bolts C implementation to a Codex agent, with me providing what kind of boundaries I wanted between files, and some style & documentation preferences.

I also provided it a very detailed set of instructions for what it was to build. And described how I wanted tests implemented. I then let it do most of the work for like 20-ish minutes, and came back and verified the results. 

## Design
The client has three layers, each divided into seperate header files & corresponding implementations. 
- protocol.c : This layer handles pure protocol logic. It parses SMTP servers replies, provides functions for formatting SMTP commands, and builds the header and body for messages.
- session.c : This layer orchestrated the SMTP conversation. It sends SMTP commands and checks that the server replies. It does so using read/write callback provided in a transport struct. This allows the session to operate with any generic "socket-like" data.
- socket_transport.c : This layer resolves the server's hostname, establishes a TCP connection, and sets up the callback functions that will interact with a socket.
