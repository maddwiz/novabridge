# syntax=docker/dockerfile:1.7

FROM python:3.11-slim AS sdk-builder

WORKDIR /build/python-sdk
COPY python-sdk/ /build/python-sdk/
RUN python -m pip install --no-cache-dir --upgrade pip build && \
    python -m build --wheel --outdir /dist

FROM python:3.11-slim AS runtime

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    NOVABRIDGE_CONTAINER_MODE=mock \
    NOVABRIDGE_DOCKER_PORT=8080

WORKDIR /app

COPY --from=sdk-builder /dist /dist
COPY mcp-server/ /app/mcp-server/
COPY docker/ /app/docker/

RUN python -m pip install --no-cache-dir --upgrade pip && \
    python -m pip install --no-cache-dir /dist/*.whl && \
    python -m pip install --no-cache-dir -r /app/mcp-server/requirements.txt

EXPOSE 8080
ENTRYPOINT ["/app/docker/entrypoint.sh"]
CMD []
