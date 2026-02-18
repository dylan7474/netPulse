SHELL := /bin/bash

.PHONY: help verify serve build-c clean

help:
	@echo "Available targets:"
	@echo "  make verify  - Run local validation checks"
	@echo "  make serve   - Start a local static server on :8080"
	@echo "  make build-c - Build the Linux C monitor executable"
	@echo "  make clean   - No-op placeholder for future generated files"

verify:
	@./configure
	@test -f index.html
	@echo "Verification complete: index.html found and required tooling available."

serve:
	@python3 -m http.server 8080

build-c:
	@gcc -O2 -Wall -Wextra -std=c11 netpulse.c -o netpulse-c $(shell pkg-config --cflags --libs gtk+-3.0)
	@echo "Built ./netpulse-c"


clean:
	@echo "Nothing to clean."
