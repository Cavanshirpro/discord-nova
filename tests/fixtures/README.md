# Local TLS fixture material

These certificates and the localhost private key are public, disposable test
material, generated for the loopback tests. They must never be used to identify
a production service. Nothing is installed in an operating-system trust store.

`localhost.pem` is self-signed and valid only for the DNS name `localhost`.
`untrusted.pem` is a different self-signed certificate. The test verifies that
an unrelated trust root and a hostname mismatch both reject the connection
before authenticated HTTP can be sent.
