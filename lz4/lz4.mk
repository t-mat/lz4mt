$(eval $(call library,xxhash,xxhash.c))
$(eval $(call library,lz4,lz4.c lz4hc.c,xxhash))

$(eval $(call program,lz4,lz4 xxhash,lz4cli.c bench.c))