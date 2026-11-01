FROM debian:bookworm-slim
RUN apt-get update -qq && apt-get install -y -qq g++ cmake qt6-base-dev qt6-svg-dev qt6-tools-dev-tools libgl1-mesa-dev fonts-inter && rm -rf /var/lib/apt/lists/*
WORKDIR /src
