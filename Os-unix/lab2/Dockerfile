FROM debian:bookworm-slim
COPY worker.sh /usr/local/bin/worker.sh
RUN chmod +x /usr/local/bin/worker.sh
ENTRYPOINT ["/usr/local/bin/worker.sh"]
