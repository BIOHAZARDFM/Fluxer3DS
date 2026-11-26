# CA certificate bundle

`cacert.pem` is the Mozilla root CA certificate collection distributed by
the Python Certifi project. It is used by libcurl for HTTPS peer verification.
Certifi's certificate bundle is licensed under the Mozilla Public License 2.0
(MPL-2.0). The bundle is embedded into the 3DSX at build time and written to
`sdmc:/3ds/Fluxer3DS/data/ssl/cacert.pem` on first launch.

Upstream: https://github.com/certifi/python-certifi
