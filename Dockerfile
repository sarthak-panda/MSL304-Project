# FlowSim web app: builds the C++ engine, then serves the Streamlit UI.
FROM python:3.12-slim-bookworm AS engine
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY tests tests
COPY bench bench
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFLOWSIM_BUILD_TESTS=OFF -DFLOWSIM_BUILD_BENCH=OFF \
 && cmake --build build --parallel --target flowsim_cli

FROM python:3.12-slim-bookworm
WORKDIR /app
COPY ui/requirements.txt ui/requirements.txt
RUN pip install --no-cache-dir -r ui/requirements.txt
COPY ui ui
COPY config config
COPY data data
COPY --from=engine /src/build/flowsim_cli build/flowsim_cli
ENV FLOWSIM_CLI=/app/build/flowsim_cli PORT=8501
EXPOSE 8501
WORKDIR /app/ui
CMD ["sh", "-c", "streamlit run app.py --server.address 0.0.0.0 --server.port ${PORT}"]
