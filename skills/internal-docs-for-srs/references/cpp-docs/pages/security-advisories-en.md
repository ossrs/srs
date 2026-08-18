# SRS Security

Please report any security vulnerabilities to [here](https://github.com/ossrs/srs/security/advisories).

## CVE-2024-29882

HTTP API: DOM - XSS on JSONP callback

* Severity: **High**
* Advisory: [GHSA-gv9r-qcjc-5hj7](https://github.com/ossrs/srs/security/advisories/GHSA-gv9r-qcjc-5hj7)
* [CVE-2024-29882](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2024-29882)
* Not vulnerable: 5.0.210+, 6.0.121+
* Vulnerable: 5.0.0-5.0.210, 6.0.0-6.0.121
* The patch: [c75c9840d](https://github.com/ossrs/srs/commit/c75c9840d533a1a2c7aaf18f7bd7990ef0cbecfa) (v5.0.210), [244ce7bc0](https://github.com/ossrs/srs/commit/244ce7bc013a0b805274a65132a2980680ba6b9d) (v6.0.121)
* Fixed at: 2024.03.28

## CVE-2023-34105

Command injection in demonstration api-server for HTTP callback.

* Severity: **High**
* Advisory: [GHSA-vpr5-779c-cx62](https://github.com/ossrs/srs/security/advisories/GHSA-vpr5-779c-cx62)
* [CVE-2023-34105](http://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2023-34105)
* Not vulnerable: v5.0.157+, v5.0-b1+, v6.0.48+
* Vulnerable: v5.0.137-v5.0.156, v6.0.18-v6.0.47
* The patch: [1e43bb6](https://github.com/ossrs/srs/commit/1e43bb6b9fe7d6e0d5ffda6410d1206e8e7c1fb1) (v5.0.157), [1d878c2](https://github.com/ossrs/srs/commit/1d878c2daaf913ad01c6d0bc2f247116c8050338) (v6.0.48)
* Fixed at: 2023.07.05

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/pages/security-advisories-en)
