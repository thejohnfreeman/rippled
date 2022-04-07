#!/usr/bin/env bash
conan source . --source-folder=tmp/source && \
conan export external/rocksdb/ && \
conan install . --install-folder=tmp/build && \
conan build . --source-folder=tmp/source --build-folder=tmp/build && \
conan package . --source-folder=tmp/source --build-folder=tmp/build --package-folder=tmp/package && \
conan export-pkg . --package-folder=tmp/package
