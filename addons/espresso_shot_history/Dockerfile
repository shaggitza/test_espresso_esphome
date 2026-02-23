ARG BUILD_FROM=python:3.12-alpine
FROM $BUILD_FROM

# Copy the uv binary from the official image — no pip required on the base image
COPY --from=ghcr.io/astral-sh/uv:0.10.4 /uv /usr/local/bin/uv

WORKDIR /app

COPY pyproject.toml /app/
RUN uv sync --no-cache --no-dev

COPY shot_history.py /app/

CMD ["uv", "run", "python3", "-u", "/app/shot_history.py"]
