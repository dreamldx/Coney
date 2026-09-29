# Convenience wrapper around CMake, plus deployment to a Pi.
#
#   make            build
#   make deploy     build, render deploy/pi/*.in, install the systemd unit, enable and (re)start it
#   make undeploy   stop and remove the unit
#   make render     only render the templates into build/deploy/
#
# Variables (override on the command line, e.g. `make deploy PROD_DIR=/srv/coney`):

BUILD_DIR ?= build
JOBS      ?= 3
PROD_DIR  ?= /opt/coney/prod
SUDO      ?= sudo

BUILD_BIN := $(abspath $(BUILD_DIR))/coney
UNIT      := coney.service
RENDERED  := $(BUILD_DIR)/deploy/$(UNIT)

.PHONY: all build render deploy undeploy clean

all: build

build:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j$(JOBS)

# Render every template. Add more `@VAR@` substitutions here if a template needs them.
$(RENDERED): deploy/pi/$(UNIT).in Makefile
	@mkdir -p $(dir $@)
	sed -e 's|@BUILD_BIN@|$(BUILD_BIN)|g' -e 's|@PROD_DIR@|$(PROD_DIR)|g' $< > $@
	@echo "rendered $@"

render: $(RENDERED)

deploy: build $(RENDERED)
	$(SUDO) install -d -m 0755 $(PROD_DIR)
	$(SUDO) install -D -m 0644 $(RENDERED) /etc/systemd/system/$(UNIT)
	$(SUDO) systemctl daemon-reload
	$(SUDO) systemctl enable $(UNIT)
	$(SUDO) systemctl restart $(UNIT)
	@echo "deployed: $(UNIT) enabled and restarted (runs $(PROD_DIR)/coney as root)"

undeploy:
	-$(SUDO) systemctl disable --now $(UNIT)
	$(SUDO) rm -f /etc/systemd/system/$(UNIT)
	$(SUDO) systemctl daemon-reload
	@echo "removed $(UNIT); $(PROD_DIR) was left in place"

clean:
	rm -rf $(BUILD_DIR)
