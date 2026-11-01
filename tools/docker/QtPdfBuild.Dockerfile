FROM lumen-qt-build
RUN apt-get update -qq && apt-get install -y -qq qt6-pdf-dev && rm -rf /var/lib/apt/lists/*
