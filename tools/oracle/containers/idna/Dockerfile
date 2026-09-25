# python-idna, the independent IDNA2008 implementation `check-idna-oracle`
# compares this library's derivation against.
#
# **Built here, not pulled, and the reason is the tables.** `idna` vendors its
# own generated UCD data, so `idna.idnadata.__version__` is a property of the
# *package* and no interpreter can change it. Debian 13 packages 3.10, whose
# tables are 15.1.0 - two releases behind this library's 17.0.0, which is the
# whole of the 925-codepoint blind spot that gate used to print. 3.19 carries
# 17.0.0 exactly, and there is no stock image that has it.
#
# The base is pinned by digest and the wheel by SHA-256, so the two things that
# decide the answers - the tables and the code reading them - are both bytes
# rather than names. The interpreter's own `unicodedata` is deliberately *not*
# part of the claim: nothing in the comparison reads it, because assignedness
# comes from the UCD this library pins and idna's own version is asserted to
# match it. See tools/oracle/idna_ask.py.
FROM docker.io/library/python@sha256:7bf6c3111fe094f8ee1a1cbcdc63c4cfb345b0e3df42d5aa9a90b3b4b022ab6d

COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir --require-hashes --only-binary :all: \
        -r /tmp/requirements.txt \
 && rm /tmp/requirements.txt \
 && python3 -c "import idna.idnadata, idna.uts46data; \
        print('idnadata', idna.idnadata.__version__, \
              'uts46data', idna.uts46data.__version__)"
