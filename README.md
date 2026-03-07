# Chat Server

Вариант 3

## Commands
- \users lists online users
- \quit <message> quits the chat and notifies online users

## Usage
- ./1 0.0.0.0 10001 to launch server
- nc localhost 10001 to connect to chat
- To join the chat you should enter your username

## Using
- epoll() for non-blocking traffic control
