#
# Main Makefile. This is basically the same as a component makefile.
#
COMPONENT_EMBED_FILES := assets/style.css assets/jquery.gz assets/code.js assets/index.html

ifdef CONFIG_ENABLE_SECURITY_PROTO
COMPONENT_EMBED_TXTFILES += certs/cacert.pem certs/cert.pem certs/privkey.pem
endif
