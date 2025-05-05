@echo off

start "" wsl bash -c "./server 9999; exec bash"
timeout /t 1 > nul
start "" wsl bash -c " ./client 127.0.0.1 9999; exec bash"
timeout /t 1 > nul
start "" wsl bash -c " ./client 127.0.0.1 9999; exec bash"