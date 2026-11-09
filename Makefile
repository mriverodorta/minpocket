.PHONY: check native audit setup-hooks
check:
	./scripts/check.sh
native:
	./scripts/build-native.sh
audit:
	python3 scripts/audit-public.py
setup-hooks:
	git config core.hooksPath .githooks
