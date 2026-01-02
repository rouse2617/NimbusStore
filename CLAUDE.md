# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

NebulaStore 2.0 is an S3-compatible object storage system for AI training workloads, written in C++20. It fuses design principles from JuiceFS, CubeFS, DeepSeek 3FS, and Baidu's Canghai storage system.

## Build Commands

```bash
# Build all targets (main executable, tests, logger test)
make all

# Build with CMake (alternative)
mkdir build && cd build && cmake .. && make -j$(nproc)

# Run tests
make test              # or ./build/nebula-test
make logger-test       # or ./build/logger-test

# Clean build artifacts
make clean
```

## Architecture

### Core Components

- **Metadata Service** (`include/nebulastore/metadata/`): Inode-based file/directory operations with RocksDB persistence. Uses range-based partitioning (CubeFS style) for scalability.

- **Storage Backend** (`include/nebulastore/storage/`): Pluggable backends (S3, local filesystem). Supports JuiceFS-style slice operations and batch reads for AI workloads.

- **Unified Namespace** (`include/nebulastore/namespace/`): Bidirectional path translation between S3 (`s3://bucket/path`) and POSIX (`/path`) formats.

- **Protocol Gateway** (`include/nebulastore/protocol/`): HTTP server (mongoose-based) for S3 API compatibility.

### Key Abstractions

- `InodeID`: 64-bit file identifier
- `SliceInfo`: Chunk metadata for file data (offset, size, storage key)
- `FileLayout`: Collection of slices comprising a file
- `MetaPartition`: Inode range partition for distributed metadata

### Data Flow

1. Client request → Protocol Gateway (HTTP/FUSE)
2. Path lookup → Namespace Service (path conversion)
3. Metadata ops → MetadataService → RocksDB
4. Data ops → StorageBackend (S3/Local)

## Dependencies

- RocksDB 9.1.0 (at `/home/hrp/yig/rocksdb-9.1.0/`)
- Ceph 17.2.6 (at `/home/hrp/yig/ceph-17.2.6/`)
- pthread, zlib, stdc++fs

## Configuration

Config files in `configs/`: `metadata.yaml`, `s3_gateway.yaml`, `fuse.yaml`

## Running

```bash
./build/nebula-master --config ../configs/master.yaml
```
