# syntax=docker/dockerfile:1.4

ARG PYTHON_VERSION=3.12

FROM python:${PYTHON_VERSION}-slim-bookworm AS builder

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    DJANGO_ENV=production

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        build-essential \
        libffi-dev \
        libxml2-dev \
        libxmlsec1-dev \
        libxmlsec1-openssl \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY requirements.txt ./

RUN python -m venv /opt/venv \
    && /opt/venv/bin/pip install --upgrade pip \
    && /opt/venv/bin/pip install --no-cache-dir -r requirements.txt


FROM python:${PYTHON_VERSION}-slim-bookworm AS runtime

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PATH="/opt/venv/bin:$PATH" \
    PORT=8000 \
    WEBSITES_PORT=8000 \
    PYTHONPATH="/home/site/wwwroot"

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libxml2 \
        libxmlsec1 \
        libxmlsec1-openssl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system app \
    && useradd --system --gid app --home /home/site/wwwroot app

COPY --from=builder /opt/venv /opt/venv

WORKDIR /home/site/wwwroot

COPY . .

RUN mkdir -p /home/site/wwwroot/logs \
    && chown -R app:app /home/site/wwwroot

USER app

EXPOSE 8000

RUN python manage.py collectstatic --noinput

CMD ["gunicorn", "gellert_project.wsgi", "--logger-class", "gellert_project.gunicorn.Logger", "--timeout", "45", "--access-logfile", "-", "--error-logfile", "-", "--chdir", "/home/site/wwwroot", "--workers", "6", "--access-logformat", "%(h)s %(L)s %(r)s %(s)s %(b)s %(f)s %(a)s"]