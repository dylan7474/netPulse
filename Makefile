SHELL := /bin/bash

.PHONY: help verify serve clean

help:
	@echo "Available targets:"
	@echo "  make verify  - Run local validation checks"
	@echo "  make serve   - Start a local static server on :8080"
	@echo "  make clean   - No-op placeholder for future generated files"

verify:
	@./configure
	@test -f index.html
	@echo "Verification complete: index.html found and required tooling available."

serve:
	@python3 -m http.server 8080

clean:
	@echo "Nothing to clean."
