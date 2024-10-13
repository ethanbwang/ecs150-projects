#!/usr/bin/env bash

# This test runs the basic curl commands that are provided in the README

# Run server in background
./gunrock_web &
PID=$!

# Get a basic HTML file
curl http://localhost:8080/hello_world.html

# Get a basic HTML file with more detailed information
curl -v http://localhost:8080/hello_world.html

# Head a basic HTML file
curl --head http://localhost:8080/hello_world.html

# Test out a file that does not exist (404 status code)
curl -v http://localhost:8080/hello_world2.html

# Test out a POST, which isn't supported currently (405 status code)
curl -v -X POST http://localhost:8080/hello_world.html

# Kill server
kill -9 $PID
