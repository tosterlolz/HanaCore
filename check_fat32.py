#!/usr/bin/env python3
import struct

# Read rootfs.img
with open('./build/rootfs.img', 'rb') as f:
    data = f.read()

# Parse BPB (Boot Parameter Block) at offset 0
bytes_per_sec = struct.unpack('<H', data[11:13])[0]
secs_per_clus = struct.unpack('<B', data[13:14])[0]
reserved_secs = struct.unpack('<H', data[14:16])[0]
num_fats = struct.unpack('<B', data[16:17])[0]
fat_sz32 = struct.unpack('<I', data[36:40])[0]
root_clus = struct.unpack('<I', data[44:48])[0]

print(f"Bytes per sector: {bytes_per_sec}")
print(f"Sectors per cluster: {secs_per_clus}")
print(f"Reserved sectors: {reserved_secs}")
print(f"Number of FATs: {num_fats}")
print(f"FAT size (sectors): {fat_sz32}")
print(f"Root cluster: {root_clus}")

# Calculate where cluster 2 starts
cluster_begin_lba = reserved_secs + num_fats * fat_sz32
cluster2_lba = cluster_begin_lba + (root_clus - 2) * secs_per_clus
cluster2_offset = cluster2_lba * bytes_per_sec

print(f"\nCluster begin LBA: {cluster_begin_lba}")
print(f"Cluster 2 LBA: {cluster2_lba}")
print(f"Cluster 2 offset: {cluster2_offset} (0x{cluster2_offset:x})")

# Show what's at that offset
print(f"\nFirst 32 bytes of cluster 2:")
cluster_data = data[cluster2_offset:cluster2_offset+32]
print(' '.join(f'{b:02x}' for b in cluster_data))
print('ASCII:', ''.join(chr(b) if 32 <= b < 127 else '.' for b in cluster_data))
