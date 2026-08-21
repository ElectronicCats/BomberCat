# Local nfcgate-server for BomberCat relay testing.
#
# Build context is the vendored server clone (repo ./server). Pinned to the
# committed *_pb2.py, which require the classic protobuf runtime (<= 3.20.x).
#
#   docker build -f tools/testserver/Dockerfile -t bombercat-nfcgate-server server
#   docker run --rm -p 5566:5566 bombercat-nfcgate-server
#
# (or just run tools/testserver/run.sh)
FROM python:3.11-slim

ENV PYTHONUNBUFFERED=1
WORKDIR /srv

RUN pip install --no-cache-dir "protobuf==3.20.3"

COPY server.py /srv/
COPY plugins/ /srv/plugins/

EXPOSE 5566

# Load the "log" plugin so relayed APDUs are printed to stdout.
ENTRYPOINT ["python", "server.py"]
CMD ["log"]
