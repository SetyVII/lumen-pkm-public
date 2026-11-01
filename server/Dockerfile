# syntax=docker/dockerfile:1
FROM golang:1.26-bookworm AS go-build
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY *.go ./
RUN --mount=type=cache,target=/root/.cache/go-build CGO_ENABLED=1 go test -race ./... && CGO_ENABLED=1 go build -trimpath -ldflags="-s -w" -o /lumen .

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=go-build /lumen /app/lumen
RUN mkdir -p /data/lumen && chown -R 1000:1000 /data
USER 1000:1000
ENV LUMEN_LISTEN=0.0.0.0:8765 LUMEN_DATA_DIR=/data/lumen LUMEN_KEY_FILE=/run/secrets/encryption_key LUMEN_TOKEN_FILE=/run/secrets/api_token
EXPOSE 8765
HEALTHCHECK --interval=30s --timeout=10s --start-period=30s CMD ["/app/lumen", "-healthcheck"]
ENTRYPOINT ["/app/lumen"]
