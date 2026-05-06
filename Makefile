# ========== VAULTHALLA MAKEFILE ==========
# ⚒️  Forge build & deployment targets
# =========================================

.PHONY: \
	all \
	build \
	install \
	dev \
	test \
	run_test \
	run-test \
	clean-test \
	deb \
	release \
	doctor \
	uninstall \
	clean \
	clean-full \
	clean-package \
	purge-builds

dev test run_test run-test clean-test: export VH_BUILD_MODE=dev

# Default target: build
all: build

## 🔨 Build via Conan
build:
	@echo "🔧 Building Vaulthalla..."
	conan install . -r vaulthalla --build=missing
	conan build .

## 🛠️ Install system-wide
install:
	./bin/setup/install_guard.sh
	@echo "🛡️  Running install script..."
	./bin/install.sh

## 🛠️ Install in development mode
dev:
	./bin/uninstall.sh
	./bin/setup/install_guard.sh
	@echo "🛡️  Running install script..."
	./bin/install.sh

test:
	./bin/tests/uninstall.sh
	@echo "🛡️  Running integration tests install script..."
	./bin/tests/install.sh

run_test:
	./bin/tests/uninstall.sh
	@echo "🛡️  Running integration tests install script..."
	./bin/tests/install.sh --run

clean-test:
	@echo "💣 Running integration tests uninstall script..."
	./bin/tests/uninstall.sh

deb:
	@echo "🔧 Building Debian package..."
	./bin/install_deb.sh

release:
	@echo "🔧 Building release package..."
	./bin/install_deb.sh --push

doctor:
	@echo "🔍 Running doctor script..."
	./bin/doctor.sh

## 🧼 Uninstall everything
uninstall:
	@echo "💣 Running uninstall script..."
	./bin/uninstall.sh
	./bin/tests/uninstall.sh

clean:
	./bin/clean.sh

clean-full purge-builds:
	./bin/clean.sh --full

clean-package:
	./bin/clean.sh --package
