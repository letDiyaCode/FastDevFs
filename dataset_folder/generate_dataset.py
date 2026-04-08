#!/usr/bin/env python3
"""
Generate a dataset of library folder names (100,000+ entries).
Each entry has: folder_name, is_library (1 = library, 0 = not library)

Sources:
- Top PyPI packages (real-world Python libraries)
- Popular npm packages (real-world JS libraries)
- Popular Java/Maven libraries
- Popular Ruby gems
- Popular Go modules
- Popular Rust crates
- Popular C/C++ libraries
- Known library directory patterns (node_modules, vendor, etc.)
- Non-library folder names (src, test, docs, config, etc.)
"""

import csv
import random
import hashlib
import string
import os

random.seed(42)

# ============================================================================
# REAL LIBRARY NAMES FROM VARIOUS ECOSYSTEMS
# ============================================================================

# Top PyPI packages (from hugovk.github.io/top-pypi-packages)
PYPI_PACKAGES = [
    "boto3", "packaging", "setuptools", "urllib3", "certifi", "typing-extensions",
    "requests", "charset-normalizer", "idna", "botocore", "aiobotocore",
    "python-dateutil", "cryptography", "six", "numpy", "cffi", "pyyaml",
    "grpcio-status", "pycparser", "pydantic", "pluggy", "s3transfer",
    "pygments", "click", "attrs", "protobuf", "pydantic-core", "anyio",
    "fsspec", "pandas", "pytest", "h11", "markupsafe", "iniconfig", "s3fs",
    "platformdirs", "annotated-types", "pip", "wheel", "jinja2", "jmespath",
    "importlib-metadata", "filelock", "pathspec", "pyjwt", "httpx",
    "typing-inspection", "python-dotenv", "httpcore", "pytz", "zipp", "rich",
    "pyasn1", "jsonschema", "yarl", "multidict", "aiohttp", "google-auth",
    "uvicorn", "markdown-it-py", "google-api-core", "tzdata", "tqdm", "tomli",
    "colorama", "googleapis-common-protos", "mdurl", "starlette", "virtualenv",
    "awscli", "pillow", "propcache", "frozenlist", "scipy", "rpds-py",
    "trove-classifiers", "fastapi", "rsa", "referencing", "wrapt",
    "pyasn1-modules", "aiosignal", "jsonschema-specifications", "greenlet",
    "grpcio", "sqlalchemy", "requests-oauthlib", "pyarrow", "pyparsing",
    "aiohappyeyeballs", "opentelemetry-api", "tenacity", "cachetools", "regex",
    "psutil", "opentelemetry-semantic-conventions", "hatchling", "oauthlib",
    "opentelemetry-sdk", "sniffio", "more-itertools", "soupsieve",
    "shellingham", "websockets", "exceptiongroup", "docutils",
    "beautifulsoup4", "tomlkit", "lxml", "typer", "distlib", "grpcio-tools",
    "et-xmlfile", "openpyxl", "requests-toolbelt", "mypy-extensions",
    "pydantic-settings", "networkx", "dnspython", "proto-plus",
    "websocket-client", "coverage", "werkzeug", "python-multipart", "msgpack",
    "pyopenssl", "openai", "langchain", "google-cloud-storage", "distro",
    "flask", "psycopg2-binary", "pynacl", "tabulate", "wcwidth",
    "keyring", "huggingface-hub", "smmap", "sortedcontainers",
    "scikit-learn", "decorator", "fonttools", "isodate", "watchfiles",
    "matplotlib", "joblib", "ruff", "poetry-core", "jiter", "redis",
    "kiwisolver", "gitpython", "azure-core", "azure-identity", "ptyprocess",
    "pexpect", "bcrypt", "email-validator", "itsdangerous", "threadpoolctl",
    "editables", "msal", "pytest-cov", "google-cloud-core", "alembic",
    "zstandard", "sse-starlette", "contourpy", "prompt-toolkit", "ruamel-yaml",
    "defusedxml", "async-timeout", "orjson", "textual", "gitdb", "sympy",
    "deprecated", "pytest-asyncio", "blinker", "google-crc32c", "docker",
    "rapidfuzz", "google-resumable-media", "mpmath", "tzlocal", "chardet",
    "cycler", "kubernetes", "mako", "google-api-python-client", "dill",
    "setuptools-scm", "prometheus-client", "backoff", "paramiko", "sentry-sdk",
    "marshmallow", "uritemplate", "fastjsonschema", "google-cloud-bigquery",
    "mypy", "tokenizers", "uvloop", "google-auth-httplib2", "nodeenv",
    "httplib2", "sqlparse", "transformers", "toml", "ipython",
    "msal-extensions", "authlib", "babel", "black", "tiktoken",
    "azure-storage-blob", "xmltodict", "httptools", "tornado", "cython",
    "aiofiles", "pre-commit", "cloudpickle", "identify", "gunicorn",
    "parso", "traitlets", "executing", "asgiref", "jedi", "asttokens",
    "importlib-resources", "pytest-xdist", "matplotlib-inline", "py4j",
    "execnet", "python-json-logger", "langchain-core", "markdown",
    "cachecontrol", "webencodings", "nest-asyncio", "xxhash", "multiprocess",
    "typing-inspect", "isort", "h2", "gcsfs", "hyperframe", "hpack",
    "dbt-core", "termcolor", "watchdog", "pymongo", "flatbuffers",
    "pyperclip", "pytest-mock", "debugpy", "dulwich", "pkginfo", "torch",
    "aioitertools", "mccabe", "installer", "pyzmq", "pymysql",
    "dataclasses-json", "pymupdf", "anthropic", "beartype", "jsonpath-ng",
    "jsonref", "slack-sdk", "pycryptodome", "poetry", "lz4", "onnxruntime",
    "ruamel-yaml-clib", "cattrs", "tree-sitter", "pycodestyle",
    "pygithub", "msrest", "deepdiff", "langsmith", "semver",
    "tinycss2", "future", "sphinx", "smart-open", "invoke",
    "databricks-sql-connector", "xlsxwriter", "arrow", "pendulum", "narwhals",
    "wsproto", "jupyter-core", "datasets", "loguru", "shapely",
    "argcomplete", "safetensors", "structlog", "ipykernel",
    "faker", "docker", "simplejson", "mistune", "comm", "lark",
    "opensearch-py", "brotli", "pysocks", "croniter", "typeguard",
    "responses", "pyflakes", "asyncpg", "bleach", "sqlmodel",
    "nltk", "zope-interface", "argon2-cffi", "plotly", "portalocker",
    "llvmlite", "numba", "toolz", "flake8", "selenium", "json5",
    "celery", "elasticsearch", "confluent-kafka", "pylint", "ray",
    "pyspark", "kombu", "humanize", "trio", "google-cloud-pubsub",
    "humanfriendly", "asyncio", "jupyterlab", "playwright",
    "openapi-spec-validator", "jupyter-server", "flask-cors", "altair",
    "django", "opencv-python", "pypdf", "seaborn", "xgboost", "h5py",
    "prettytable", "aiosqlite", "graphql-core", "polars",
    "mysql-connector-python", "azure-keyvault-secrets", "streamlit",
    "hypothesis", "google-pasta", "ml-dtypes", "sendgrid",
    "elastic-transport", "validators", "peewee", "phonenumbers",
    "tldextract", "apscheduler", "frozendict", "ninja", "flit-core",
    "passlib", "reportlab", "google-cloud-firestore", "pyright",
    "simple-salesforce", "markdownify", "pybind11",
    "yamllint", "great-expectations", "pyiceberg", "tensorboard",
    "gunicorn", "circus", "waitress", "gevent", "eventlet",
    "twisted", "scrapy", "celery", "dramatiq", "huey", "rq",
    "falcon", "bottle", "cherrypy", "sanic", "aiohttp", "quart",
    "dash", "gradio", "panel", "bokeh", "holoviews",
    "tensorflow", "keras", "pytorch-lightning", "jax", "flax",
    "optax", "chex", "haiku", "trax",
    "gym", "gymnasium", "stable-baselines3",
    "spacy", "gensim", "nltk", "textblob", "flair",
    "opencv-python", "pillow", "scikit-image", "imageio",
    "librosa", "soundfile", "pydub", "audioread",
    "networkx", "igraph", "graph-tool",
    "sympy", "sage", "mpmath",
    "astropy", "biopython", "rdkit",
    "dask", "vaex", "modin", "cudf",
    "airflow", "prefect", "dagster", "luigi",
    "mlflow", "wandb", "neptune", "comet-ml",
    "pytest", "unittest", "nose", "tox", "nox",
    "sphinx", "mkdocs", "pdoc", "pydoc",
    "black", "isort", "autopep8", "yapf",
    "pylint", "flake8", "mypy", "pyright", "ruff",
    "pdb", "ipdb", "pudb", "debugpy",
    "cProfile", "line-profiler", "memory-profiler", "py-spy",
    "pipenv", "poetry", "pdm", "hatch", "flit",
    "virtualenv", "venv", "conda", "mamba",
    "docker", "podman", "kubernetes", "helm",
    "ansible", "puppet", "chef", "saltstack",
    "terraform", "pulumi", "crossplane",
    "prometheus", "grafana", "datadog", "newrelic",
    "sentry", "rollbar", "bugsnag",
    "celery", "dramatiq", "huey", "rq", "arq",
    "redis", "memcached", "rabbitmq",
    "sqlalchemy", "peewee", "tortoise-orm", "django-orm",
    "alembic", "flyway", "liquibase",
    "pydantic", "marshmallow", "cerberus", "voluptuous",
    "click", "typer", "argparse", "fire", "docopt",
    "rich", "tqdm", "alive-progress", "yaspin",
    "httpx", "requests", "aiohttp", "urllib3",
    "beautifulsoup4", "lxml", "parsel", "scrapy",
    "paramiko", "fabric", "invoke",
    "boto3", "google-cloud", "azure",
]

# Popular npm packages
NPM_PACKAGES = [
    "lodash", "chalk", "react", "express", "commander", "axios", "debug",
    "fs-extra", "glob", "inquirer", "moment", "rimraf", "mkdirp", "minimist",
    "async", "uuid", "bluebird", "yargs", "underscore", "body-parser",
    "request", "through2", "webpack", "colors", "semver", "readable-stream",
    "cross-spawn", "concat-stream", "path-exists", "jquery", "tslib",
    "core-js", "rxjs", "zone-js", "typescript", "eslint", "prettier",
    "babel-core", "babel-loader", "webpack-cli", "webpack-dev-server",
    "css-loader", "style-loader", "file-loader", "url-loader", "sass-loader",
    "postcss", "autoprefixer", "tailwindcss", "bootstrap", "material-ui",
    "react-dom", "react-router", "react-router-dom", "react-redux", "redux",
    "redux-thunk", "redux-saga", "mobx", "mobx-react", "vue", "vue-router",
    "vuex", "angular", "angular-core", "angular-common", "angular-forms",
    "angular-router", "angular-http", "next", "nuxt", "gatsby", "svelte",
    "ember", "backbone", "knockout", "mithril", "preact", "inferno",
    "lit-element", "stencil", "polymer", "socket-io", "socket-io-client",
    "mongoose", "sequelize", "typeorm", "prisma", "knex", "bookshelf",
    "pg", "mysql2", "mongodb", "redis", "ioredis", "memcached",
    "passport", "jsonwebtoken", "bcrypt", "bcryptjs", "helmet", "cors",
    "cookie-parser", "compression", "morgan", "winston", "bunyan", "pino",
    "dotenv", "config", "convict", "nconf", "joi", "yup", "zod",
    "ajv", "validator", "class-validator", "class-transformer",
    "jest", "mocha", "chai", "sinon", "jasmine", "karma", "ava",
    "tape", "supertest", "nock", "cypress", "puppeteer", "playwright",
    "selenium-webdriver", "nightwatch", "protractor", "storybook",
    "nodemon", "pm2", "forever", "concurrently", "npm-run-all",
    "husky", "lint-staged", "commitlint", "conventional-changelog",
    "lerna", "nx", "turborepo", "changesets", "rush",
    "rollup", "esbuild", "vite", "parcel", "snowpack", "wmr",
    "gulp", "grunt", "broccoli", "brunch",
    "d3", "chart-js", "three", "pixi-js", "konva", "fabric-js",
    "leaflet", "mapbox-gl", "openlayers", "cesium",
    "animate-css", "gsap", "framer-motion", "lottie-web",
    "swiper", "slick-carousel", "owl-carousel", "flickity",
    "moment-timezone", "dayjs", "date-fns", "luxon",
    "numeral", "accounting", "decimal-js", "big-js", "bignumber-js",
    "ramda", "immutable", "immer", "fp-ts",
    "graphql", "apollo-server", "apollo-client", "relay",
    "grpc", "protobuf-js", "thrift",
    "sharp", "jimp", "canvas", "pdfkit", "jspdf",
    "nodemailer", "sendgrid", "mailgun", "ses",
    "stripe", "paypal-rest-sdk", "braintree",
    "twilio", "nexmo", "vonage",
    "aws-sdk", "firebase", "firebase-admin",
    "electron", "nw-js", "tauri",
    "react-native", "expo", "ionic", "capacitor",
    "cordova", "phonegap", "nativescript",
    "next-auth", "passport-jwt", "passport-local",
    "handlebars", "ejs", "pug", "nunjucks", "mustache",
    "marked", "showdown", "markdown-it", "remark",
    "highlight-js", "prism-js", "codemirror", "ace-editor",
    "quill", "draft-js", "slate", "tiptap", "prosemirror",
    "cropper-js", "dropzone", "uppy", "filepond",
    "toastr", "sweetalert2", "notiflix", "notyf",
    "hammer-js", "interact-js", "sortable-js", "draggable",
    "i18next", "react-intl", "vue-i18n", "formatjs",
    "socket-io", "ws", "sockjs", "faye",
    "bull", "bee-queue", "agenda", "kue",
    "cheerio", "jsdom", "xmldom", "fast-xml-parser",
    "csv-parser", "papaparse", "xlsx", "exceljs",
    "archiver", "adm-zip", "node-zip", "yazl",
    "sharp", "image-size", "probe-image-size",
    "crypto-js", "tweetnacl", "libsodium", "noble",
    "nanoid", "shortid", "cuid", "ulid",
    "p-limit", "p-queue", "p-retry", "p-map",
    "execa", "shelljs", "cross-env", "env-cmd",
    "chokidar", "watchman", "nsfw", "sane",
    "ora", "listr", "cli-progress", "progress",
    "meow", "caporal", "vorpal", "oclif",
    "figlet", "ascii-art", "boxen", "terminal-kit",
    "http-proxy", "http-proxy-middleware", "node-fetch", "got",
    "superagent", "needle", "undici", "ky",
    "formidable", "multer", "busboy", "multiparty",
    "serve-static", "send", "serve-handler",
    "connect", "koa", "hapi", "restify", "fastify", "polka", "micro",
]

# Popular Java/Maven libraries
JAVA_LIBRARIES = [
    "spring-boot", "spring-core", "spring-web", "spring-data", "spring-security",
    "spring-cloud", "spring-mvc", "spring-batch", "spring-integration",
    "hibernate", "hibernate-core", "hibernate-validator",
    "jackson", "jackson-core", "jackson-databind", "jackson-annotations",
    "gson", "json-simple", "javax-json",
    "guava", "commons-lang3", "commons-io", "commons-collections",
    "commons-codec", "commons-math", "commons-text",
    "slf4j", "log4j", "logback", "jul", "tinylog",
    "junit", "testng", "mockito", "powermock", "assertj", "hamcrest",
    "maven", "gradle", "ant", "ivy",
    "tomcat", "jetty", "undertow", "netty", "grizzly",
    "mybatis", "jooq", "jdbi", "querydsl",
    "lombok", "mapstruct", "modelmapper", "dozer",
    "okhttp", "retrofit", "apache-httpclient", "jersey-client",
    "rxjava", "reactor", "vertx", "akka",
    "kafka-clients", "rabbitmq-client", "activemq", "artemis",
    "jedis", "lettuce", "redisson",
    "elasticsearch-rest-client", "solr-solrj",
    "flyway", "liquibase", "dbunit",
    "thymeleaf", "freemarker", "velocity", "jsp",
    "swagger", "springdoc", "springfox",
    "micrometer", "dropwizard-metrics", "prometheus-client",
    "ehcache", "caffeine", "hazelcast", "infinispan",
    "quartz", "spring-retry", "resilience4j",
    "bouncycastle", "jasypt", "keycloak",
    "poi", "pdfbox", "itext", "jasperreports",
    "grpc-java", "protobuf-java", "avro", "thrift",
    "spark", "hadoop", "flink", "storm", "samza",
    "hbase", "cassandra-driver", "couchbase-client",
    "lucene", "tika", "opennlp",
    "deeplearning4j", "weka", "mallet",
    "javafx", "swing", "swt",
    "joda-time", "threeten", "commons-lang",
    "cglib", "javassist", "bytebuddy", "asm",
    "aspectj", "spring-aop",
    "commons-pool2", "commons-dbcp2", "hikaricp", "c3p0",
    "snakeyaml", "jackson-dataformat-yaml",
    "jaxb", "jaxrs", "jaxws",
]

# Popular Ruby gems
RUBY_GEMS = [
    "rails", "rack", "bundler", "rspec", "devise", "puma", "nokogiri",
    "sidekiq", "resque", "delayed-job", "activerecord", "actionpack",
    "activesupport", "actionmailer", "actioncable", "activejob",
    "activestorage", "actiontext", "actionmailbox",
    "sinatra", "hanami", "grape", "padrino", "cuba",
    "pg", "mysql2", "sqlite3", "sequel", "rom",
    "redis", "dalli", "memcached",
    "rspec-rails", "minitest", "cucumber", "capybara", "factory-bot",
    "faker", "webmock", "vcr", "shoulda", "simplecov",
    "rubocop", "brakeman", "reek", "flay", "flog",
    "capistrano", "mina", "kamal",
    "carrierwave", "paperclip", "shrine", "activestorage",
    "devise", "omniauth", "doorkeeper", "pundit", "cancancan",
    "kaminari", "will-paginate", "pagy",
    "slim", "haml", "erb", "liquid",
    "sass-rails", "uglifier", "sprockets", "webpacker", "importmap",
    "turbolinks", "hotwire", "stimulus", "turbo",
    "jbuilder", "rabl", "active-model-serializers",
    "figaro", "dotenv", "credentials",
    "pry", "byebug", "better-errors", "binding-of-caller",
    "awesome-print", "hirb", "table-print",
    "rest-client", "httparty", "faraday", "typhoeus",
    "ransack", "searchkick", "elasticsearch-rails",
    "friendly-id", "babosa", "stringex",
    "scenic", "fx", "strong-migrations",
    "whenever", "clockwork", "rufus-scheduler",
    "mail", "premailer", "roadie",
    "wicked-pdf", "prawn", "hexapdf",
    "mini-magick", "image-processing", "vips",
    "aasm", "state-machines", "workflow",
    "money", "monetize", "eu-central-bank",
    "geocoder", "geokit", "rgeo",
    "i18n", "globalize", "mobility",
    "active-admin", "administrate", "trestle",
    "rollbar", "sentry-ruby", "airbrake", "honeybadger",
    "scout-apm", "skylight", "newrelic",
]

# Popular Go modules
GO_MODULES = [
    "gin", "echo", "fiber", "chi", "mux", "httprouter", "gorilla",
    "gorm", "sqlx", "ent", "bun", "pgx",
    "cobra", "viper", "pflag", "kingpin", "urfave-cli",
    "zap", "logrus", "zerolog", "slog",
    "testify", "gomock", "gocheck", "ginkgo", "gomega",
    "wire", "dig", "fx",
    "grpc-go", "protoc-gen-go", "twirp", "connect",
    "sarama", "confluent-kafka-go", "nats",
    "go-redis", "redigo", "rueidis",
    "mongo-driver", "elastic", "clickhouse-go",
    "jwt-go", "casbin", "authboss",
    "prometheus-go", "expvar", "statsd",
    "etcd", "consul", "zookeeper",
    "kubernetes-client-go", "helm", "operator-sdk",
    "terraform-plugin-sdk", "packer",
    "aws-sdk-go", "google-cloud-go", "azure-sdk-for-go",
    "colly", "goquery", "chromedp", "rod",
    "excelize", "gofpdf", "goldmark",
    "gocv", "imaging", "gg",
    "goroutine", "errgroup", "semaphore",
    "afero", "fsnotify", "godotenv",
    "go-cmp", "go-diff", "deep",
    "uuid", "ulid", "xid", "nanoid",
    "decimal", "money",
    "cron", "gocron", "robfig-cron",
    "validator", "ozzo-validation",
    "swag", "gin-swagger",
    "air", "fresh", "realize",
    "go-migrate", "goose", "atlas",
]

# Popular Rust crates
RUST_CRATES = [
    "serde", "serde-json", "serde-yaml", "tokio", "async-std", "smol",
    "actix-web", "axum", "rocket", "warp", "tide", "poem",
    "diesel", "sqlx", "sea-orm", "rusqlite",
    "reqwest", "hyper", "surf", "ureq",
    "clap", "structopt", "argh", "gumdrop",
    "log", "env-logger", "tracing", "flexi-logger",
    "rayon", "crossbeam", "parking-lot",
    "regex", "nom", "pest", "lalrpop",
    "rand", "uuid", "chrono", "time",
    "thiserror", "anyhow", "eyre", "miette",
    "tonic", "prost", "tarpc",
    "iced", "egui", "druid", "tauri",
    "image", "imageproc", "resvg",
    "ndarray", "nalgebra", "linfa",
    "walkdir", "globset", "notify",
    "toml", "config-rs", "dotenv",
    "criterion", "proptest", "quickcheck",
    "clippy", "rustfmt", "cargo-edit",
    "wasm-bindgen", "wasm-pack", "trunk",
    "bevy", "ggez", "macroquad", "fyrox",
    "polars", "arrow", "datafusion",
    "rustls", "ring", "sodiumoxide",
    "bytes", "memmap2", "tempfile",
    "indicatif", "console", "dialoguer",
    "pretty-env-logger", "fern",
    "lazy-static", "once-cell", "dashmap",
    "futures", "pin-project", "async-trait",
    "tower", "tower-http", "middleware",
]

# Popular C/C++ libraries
C_CPP_LIBRARIES = [
    "boost", "eigen", "opencv", "qt", "gtk", "wxwidgets", "fltk",
    "sdl2", "sfml", "glfw", "glew", "glad", "vulkan",
    "opengl", "directx", "metal",
    "asio", "libuv", "libevent", "libev",
    "openssl", "mbedtls", "libsodium", "boringssl",
    "zlib", "lz4", "zstd", "snappy", "brotli", "lzma",
    "sqlite3", "leveldb", "rocksdb", "lmdb", "berkeleydb",
    "protobuf", "flatbuffers", "capnproto", "msgpack", "cereal",
    "grpc", "thrift", "zeromq", "nanomsg", "nng",
    "curl", "libhttp", "cpp-httplib", "beast",
    "jsoncpp", "rapidjson", "nlohmann-json", "simdjson",
    "expat", "libxml2", "pugixml", "tinyxml2",
    "gtest", "gmock", "catch2", "doctest", "ctest",
    "cmake", "meson", "ninja", "make", "scons", "bazel",
    "conan", "vcpkg", "hunter", "cpm",
    "spdlog", "glog", "easylogging", "plog",
    "fmt", "abseil", "folly", "poco",
    "tbb", "openmp", "mpi", "cuda", "opencl",
    "tensorflow-cc", "pytorch-cpp", "onnxruntime", "tensorrt",
    "dlib", "mlpack", "shogun", "caffe",
    "pcl", "cgal", "libigl", "openmesh",
    "bullet", "box2d", "chipmunk", "ode",
    "ffmpeg", "gstreamer", "vlc", "libav",
    "portaudio", "rtaudio", "miniaudio", "fmod",
    "cairo", "skia", "nanovg", "stb",
    "freetype", "harfbuzz", "fontconfig", "pango",
    "icu", "iconv", "utf8proc",
    "pcre2", "re2", "oniguruma",
    "libjpeg", "libpng", "libtiff", "libwebp",
    "imgui", "nuklear", "nana",
    "cpprestsdk", "pistache", "drogon", "oat",
    "cpr", "restclient-cpp",
    "nlopt", "ceres-solver", "ipopt",
    "fftw", "gsl", "lapack", "blas", "atlas",
    "hdf5", "netcdf", "fits",
    "vtk", "itk", "dcmtk",
]

# Known library/dependency DIRECTORY names (these are folders that indicate libraries)
LIBRARY_DIRECTORIES = [
    "node_modules", "vendor", "lib", "libs", "library", "libraries",
    "third_party", "third-party", "thirdparty", "3rdparty",
    "external", "externals", "extern", "ext",
    "deps", "dependencies", "packages", "pkg",
    "bower_components", "jspm_packages",
    "site-packages", "dist-packages",
    ".venv", "venv", "env", ".env", "virtualenv",
    "__pycache__", ".tox", ".nox", ".mypy_cache", ".pytest_cache",
    ".cache", "cache",
    "target", "build", "dist", "out", "output", "bin",
    ".gradle", ".maven", ".m2", ".ivy2",
    "Pods", "Carthage", "DerivedData",
    "nuget", ".nuget", "paket-files",
    "composer-vendor", "go-vendor",
    "cargo-registry", ".cargo",
    "elm-stuff", ".stack-work", ".cabal",
    "renv", "packrat",
    "bundle", "gems",
    "modules", "addons", "plugins",
    "contrib", "submodules",
    "sdk", "framework", "frameworks",
    "runtime", "runtimes",
    "toolchain", "toolchains",
    "sysroot", "staging",
]

# ============================================================================
# NON-LIBRARY FOLDER NAMES (is_library = 0)
# ============================================================================

# Common project structure directories
PROJECT_DIRS = [
    "src", "source", "sources", "app", "application", "main",
    "test", "tests", "testing", "spec", "specs", "e2e",
    "doc", "docs", "documentation", "wiki", "guides", "tutorials",
    "config", "configs", "configuration", "conf", "settings",
    "script", "scripts", "tools", "utils", "utilities", "helpers",
    "example", "examples", "sample", "samples", "demo", "demos",
    "asset", "assets", "resource", "resources", "res", "static",
    "public", "www", "wwwroot", "htdocs", "web", "webroot",
    "data", "database", "db", "migrations", "seeds", "fixtures",
    "media", "images", "img", "icons", "fonts", "styles", "css",
    "js", "javascript", "typescript", "ts",
    "template", "templates", "views", "layouts", "pages", "components",
    "model", "models", "entity", "entities", "schema", "schemas",
    "controller", "controllers", "handler", "handlers",
    "service", "services", "provider", "providers",
    "middleware", "middlewares", "interceptor", "interceptors",
    "route", "routes", "router", "routers", "api",
    "store", "stores", "state", "actions", "reducers",
    "hook", "hooks", "composable", "composables",
    "mixin", "mixins", "trait", "traits",
    "interface", "interfaces", "type", "types", "typings",
    "enum", "enums", "constant", "constants",
    "util", "helper", "common", "shared", "core", "base",
    "feature", "features", "module", "domain", "domains",
    "infra", "infrastructure", "platform",
    "deploy", "deployment", "deployments", "devops", "ops",
    "ci", "cd", "pipeline", "pipelines", "workflow", "workflows",
    "docker", "dockerfiles", "k8s", "kubernetes", "helm",
    "terraform", "ansible", "puppet", "chef",
    "monitor", "monitoring", "observability", "metrics",
    "log", "logs", "logging",
    "backup", "backups", "archive", "archives",
    "temp", "tmp", "scratch", "sandbox",
    "draft", "drafts", "wip", "experimental",
    "proto", "protos", "protobuf", "grpc",
    "graphql", "rest", "soap", "rpc",
    "auth", "authentication", "authorization", "security",
    "user", "users", "account", "accounts", "profile", "profiles",
    "admin", "dashboard", "panel", "console",
    "notification", "notifications", "email", "emails", "mail",
    "payment", "payments", "billing", "invoice", "invoices",
    "order", "orders", "cart", "checkout",
    "product", "products", "catalog", "inventory",
    "search", "analytics", "report", "reports", "reporting",
    "chat", "messaging", "message", "messages",
    "file", "files", "upload", "uploads", "download", "downloads",
    "integration", "integrations", "webhook", "webhooks",
    "plugin", "extension", "extensions", "addon",
    "theme", "themes", "skin", "skins",
    "locale", "locales", "i18n", "l10n", "translation", "translations",
    "migration", "seed", "fixture",
    "mock", "mocks", "stub", "stubs", "fake", "fakes",
    "factory", "factories",
    "benchmark", "benchmarks", "perf", "performance",
    "snapshot", "snapshots",
    "coverage", "report",
    "vendor-custom", "patches", "patch",
    "init", "setup", "install", "installer",
    "generator", "generators", "scaffold", "scaffolds",
    "task", "tasks", "job", "jobs", "worker", "workers", "queue", "queues",
    "event", "events", "listener", "listeners", "subscriber", "subscribers",
    "command", "commands", "cli",
    "kernel", "bootstrap", "startup",
    "error", "errors", "exception", "exceptions",
    "dto", "dtos", "vo", "request", "response",
    "mapper", "mappers", "converter", "converters",
    "repository", "repositories", "dao",
    "gateway", "gateways", "adapter", "adapters",
    "decorator", "decorators", "observer", "observers",
    "strategy", "strategies", "policy", "policies",
    "validator", "validators", "filter", "filters",
    "transformer", "transformers", "serializer", "serializers",
    "manager", "managers", "registry",
    "cache", "caching",
    "proxy", "proxies",
    "wrapper", "wrappers",
    "client", "clients", "server", "servers",
    "agent", "agents", "bot", "bots",
    "crawler", "scrapers", "spider", "spiders",
    "parser", "parsers", "lexer", "lexers",
    "compiler", "compilers", "interpreter", "interpreters",
    "runtime", "engine", "engines",
    "game", "games", "level", "levels",
    "scene", "scenes", "world", "worlds",
    "character", "characters", "npc", "npcs",
    "sprite", "sprites", "texture", "textures",
    "shader", "shaders", "material", "materials",
    "mesh", "meshes", "geometry",
    "audio", "sound", "sounds", "music",
    "video", "videos", "animation", "animations",
    "physics", "collision", "ai",
    "network", "networking", "socket", "sockets",
    "protocol", "protocols",
    "crypto", "encryption", "hash", "hashing",
    "math", "algorithm", "algorithms",
    "struct", "structs", "class", "classes",
    "abstract", "concrete", "implementation", "implementations",
    "internal", "private", "protected",
    "input", "output", "io",
    "reader", "readers", "writer", "writers",
    "stream", "streams", "buffer", "buffers",
    "channel", "channels", "pipe", "pipes",
    "signal", "signals", "slot", "slots",
    "debug", "trace", "profiling",
    "release", "staging", "production", "development",
    "local", "remote", "cloud",
    "mobile", "desktop", "tablet",
    "android", "ios", "windows", "macos", "linux",
    "web", "native", "hybrid",
    "frontend", "backend", "fullstack",
    "client-side", "server-side",
    "microservice", "microservices", "monolith",
    "api-gateway", "load-balancer",
    "cdn", "proxy-server",
    "databse", "nosql", "sql", "graphdb",
]

# Realistic project/business folder names
BUSINESS_DIRS = [
    "user-management", "order-service", "payment-gateway", "auth-service",
    "notification-service", "email-service", "search-service", "analytics-engine",
    "recommendation-engine", "pricing-engine", "inventory-service",
    "shipping-service", "tax-calculator", "report-generator",
    "data-pipeline", "etl-service", "batch-processor", "stream-processor",
    "api-gateway", "service-mesh", "config-server", "discovery-service",
    "feature-flags", "ab-testing", "experiment-service",
    "content-management", "media-service", "asset-manager",
    "document-service", "file-storage", "blob-storage",
    "identity-provider", "sso-service", "oauth-server",
    "audit-log", "compliance-checker", "security-scanner",
    "health-check", "status-page", "monitoring-dashboard",
    "backup-service", "disaster-recovery", "failover-manager",
    "load-testing", "stress-testing", "chaos-engineering",
    "ci-cd-pipeline", "deployment-manager", "release-manager",
    "code-review", "pull-request", "merge-queue",
    "project-tracker", "issue-tracker", "bug-tracker",
    "knowledge-base", "faq-service", "help-desk",
    "customer-portal", "admin-portal", "vendor-portal",
    "mobile-app", "web-app", "desktop-app",
    "landing-page", "marketing-site", "blog-engine",
    "forum-service", "community-platform", "social-feed",
    "chat-service", "video-call", "screen-sharing",
    "calendar-service", "scheduling-engine", "reminder-service",
    "translation-service", "localization-engine",
    "machine-learning", "deep-learning", "neural-network",
    "computer-vision", "natural-language", "speech-recognition",
    "data-visualization", "chart-engine", "graph-renderer",
    "pdf-generator", "excel-exporter", "csv-processor",
]

# Developer tool / build artifact directories (non-library)
DEV_DIRS = [
    ".git", ".svn", ".hg", ".bzr",
    ".github", ".gitlab", ".circleci", ".travis",
    ".vscode", ".idea", ".eclipse", ".atom",
    ".editorconfig", ".husky", ".changeset",
    "__tests__", "__mocks__", "__snapshots__", "__fixtures__",
    ".storybook", ".cypress", ".playwright",
    "coverage", "htmlcov", ".nyc_output",
    "node_modules_backup", "old", "deprecated", "legacy",
    "v1", "v2", "v3", "v4", "v5",
    "alpha", "beta", "rc", "canary", "nightly",
    "0.1", "0.2", "1.0", "2.0", "3.0",
    ".next", ".nuxt", ".svelte-kit", ".astro",
    ".turbo", ".nx", ".rush",
    "storybook-static", "chromatic",
    ".parcel-cache", ".rollup-cache",
    ".terraform", ".pulumi", ".serverless",
    "cdk-out", "amplify",
    ".aws-sam", "sam-build",
    ".vercel", ".netlify", ".heroku",
    ".docker", ".vagrant", ".packer",
    "chart", "charts", "manifests",
    "overlay", "overlays", "kustomize",
    "argo", "argocd", "flux",
    "prometheus-rules", "grafana-dashboards",
    "istio", "envoy", "linkerd",
]

# Realistic username/personal project folder names (non-library)
PERSONAL_DIRS = [
    "my-project", "my-app", "hello-world", "test-project",
    "playground", "experiment", "learning", "tutorial-code",
    "homework", "assignment", "lab", "practice",
    "portfolio", "resume", "personal-site", "blog",
    "notes", "journal", "diary", "todo",
    "bookmarks", "favorites", "collection",
    "dotfiles", "config-files", "setup-scripts",
    "workspace", "workbench", "studio",
    "prototype", "poc", "mvp", "proof-of-concept",
    "hackathon", "side-project", "pet-project",
    "research", "thesis", "dissertation", "paper",
    "presentation", "slides", "deck",
    "template-repo", "boilerplate", "starter-kit", "scaffold",
    "monorepo", "polyrepo", "umbrella",
]

# Words used to generate synthetic non-library names
NON_LIB_PREFIXES = [
    "my", "our", "the", "custom", "internal", "private", "local",
    "project", "app", "main", "core", "base", "home", "company",
    "team", "org", "corp", "dept", "div", "group",
    "feature", "module", "component", "widget", "screen", "page",
    "user", "admin", "system", "global", "shared",
]

NON_LIB_SUFFIXES = [
    "app", "service", "server", "client", "api", "web",
    "backend", "frontend", "mobile", "desktop",
    "core", "engine", "platform", "portal",
    "manager", "handler", "processor", "worker",
    "tool", "utility", "helper", "assistant",
    "monitor", "tracker", "dashboard", "viewer",
    "editor", "builder", "creator", "designer",
    "analyzer", "scanner", "checker", "tester",
    "config", "setup", "init", "launcher",
    "docs", "wiki", "guide", "manual",
]

# Words used to generate synthetic library-like names
LIB_PREFIXES = [
    "py", "go", "js", "ts", "rs", "rb", "php", "java", "swift", "kt",
    "lib", "fast", "easy", "simple", "tiny", "micro", "nano", "mini",
    "super", "mega", "ultra", "hyper", "turbo", "nitro", "blitz",
    "smart", "clever", "magic", "auto", "quick",
    "open", "free", "cross", "multi", "omni", "uni", "poly",
    "neo", "meta", "proto", "base", "core",
    "data", "info", "code", "dev", "api", "web", "net",
    "cloud", "edge", "node", "flux",
]

LIB_SUFFIXES = [
    "lib", "sdk", "kit", "api", "io", "js", "py",
    "ify", "ize", "ise", "ify", "or", "er", "ar",
    "orm", "db", "sql", "cache", "store", "queue",
    "http", "rpc", "grpc", "rest", "ws",
    "auth", "jwt", "oauth", "sso",
    "log", "trace", "metrics", "monitor",
    "test", "mock", "spy", "stub",
    "cli", "tui", "gui", "ui",
    "parser", "lexer", "compiler", "formatter",
    "validator", "schema", "model",
    "crypto", "hash", "cipher",
    "stream", "pipe", "channel",
    "pool", "buffer", "ring",
    "router", "proxy", "gateway",
    "mail", "smtp", "imap",
    "socket", "tcp", "udp",
]

LIB_MIDDLE_WORDS = [
    "data", "file", "string", "json", "xml", "yaml", "csv", "html",
    "image", "video", "audio", "text", "binary", "stream",
    "network", "socket", "http", "ftp", "ssh", "dns",
    "database", "table", "query", "index", "cursor",
    "config", "env", "option", "setting", "param",
    "error", "exception", "result", "response", "request",
    "event", "signal", "hook", "callback", "promise",
    "task", "job", "worker", "thread", "process",
    "color", "gradient", "animation", "transition",
    "chart", "graph", "plot", "map", "tree",
    "form", "input", "button", "dialog", "modal",
    "list", "table", "grid", "card", "panel",
    "date", "time", "schedule", "calendar", "timer",
    "user", "role", "permission", "session", "token",
    "payment", "invoice", "order", "cart", "product",
    "search", "filter", "sort", "paginate",
    "compress", "encrypt", "encode", "transform",
    "upload", "download", "sync", "backup",
    "notify", "alert", "badge", "toast",
]


def deduplicate(lst):
    """Remove duplicates while preserving order."""
    seen = set()
    result = []
    for item in lst:
        lower = item.lower().strip()
        if lower and lower not in seen:
            seen.add(lower)
            result.append(lower)
    return result


def generate_synthetic_library_names(count):
    """Generate synthetic but realistic library folder names."""
    names = set()
    
    # Pattern: prefix-word
    for _ in range(count * 3):
        if len(names) >= count:
            break
        prefix = random.choice(LIB_PREFIXES)
        middle = random.choice(LIB_MIDDLE_WORDS)
        sep = random.choice(["-", "_", ""])
        name = f"{prefix}{sep}{middle}"
        names.add(name.lower())
    
    # Pattern: word-suffix
    for _ in range(count * 3):
        if len(names) >= count:
            break
        middle = random.choice(LIB_MIDDLE_WORDS)
        suffix = random.choice(LIB_SUFFIXES)
        sep = random.choice(["-", "_", ""])
        name = f"{middle}{sep}{suffix}"
        names.add(name.lower())
    
    # Pattern: prefix-middle-suffix
    for _ in range(count * 3):
        if len(names) >= count:
            break
        prefix = random.choice(LIB_PREFIXES)
        middle = random.choice(LIB_MIDDLE_WORDS)
        suffix = random.choice(LIB_SUFFIXES)
        sep = random.choice(["-", "_"])
        name = f"{prefix}{sep}{middle}{sep}{suffix}"
        names.add(name.lower())
    
    # Pattern: word1-word2
    for _ in range(count * 3):
        if len(names) >= count:
            break
        w1 = random.choice(LIB_MIDDLE_WORDS)
        w2 = random.choice(LIB_MIDDLE_WORDS)
        if w1 != w2:
            sep = random.choice(["-", "_"])
            name = f"{w1}{sep}{w2}"
            names.add(name.lower())
    
    return list(names)[:count]


def generate_synthetic_nonlib_names(count):
    """Generate synthetic but realistic non-library folder names."""
    names = set()
    
    # Pattern: prefix-suffix
    for _ in range(count * 3):
        if len(names) >= count:
            break
        prefix = random.choice(NON_LIB_PREFIXES)
        suffix = random.choice(NON_LIB_SUFFIXES)
        sep = random.choice(["-", "_"])
        name = f"{prefix}{sep}{suffix}"
        names.add(name.lower())
    
    # Numbered versions of common dirs
    for _ in range(count * 3):
        if len(names) >= count:
            break
        base = random.choice(PROJECT_DIRS[:50])
        num = random.randint(1, 99)
        sep = random.choice(["-", "_", ""])
        name = f"{base}{sep}{num}"
        names.add(name.lower())
    
    # Compound business names
    actions = ["create", "update", "delete", "list", "get", "set", "find",
               "search", "filter", "sort", "validate", "transform", "export",
               "import", "sync", "migrate", "deploy", "build", "test", "run"]
    objects = ["user", "order", "product", "item", "file", "data", "report",
               "config", "task", "event", "message", "log", "cache", "session",
               "token", "role", "team", "project", "document", "record",
               "customer", "vendor", "partner", "employee", "account",
               "transaction", "invoice", "payment", "subscription", "plan"]
    
    for _ in range(count * 3):
        if len(names) >= count:
            break
        action = random.choice(actions)
        obj = random.choice(objects)
        sep = random.choice(["-", "_"])
        pattern = random.randint(0, 2)
        if pattern == 0:
            name = f"{action}{sep}{obj}"
        elif pattern == 1:
            name = f"{obj}{sep}{action}r"
        else:
            name = f"{obj}{sep}{random.choice(NON_LIB_SUFFIXES)}"
        names.add(name.lower())
    
    return list(names)[:count]


def generate_versioned_lib_names(base_names, count):
    """Generate versioned variants of library names."""
    names = []
    for _ in range(count):
        base = random.choice(base_names)
        version = random.choice([
            f"@{random.randint(1, 20)}.{random.randint(0, 30)}.{random.randint(0, 99)}",
            f"-v{random.randint(1, 10)}",
            f"{random.randint(2, 5)}",
            f"-{random.randint(1, 4)}.x",
        ])
        names.append(f"{base}{version}")
    return names


def generate_scoped_npm_names(count):
    """Generate @scope/package style names common in npm."""
    scopes = [
        "types", "babel", "angular", "vue", "react", "emotion", "mui",
        "jest", "testing-library", "storybook", "typescript-eslint",
        "rollup", "webpack", "vitejs", "sveltejs", "nestjs", "nrwl",
        "aws-sdk", "google-cloud", "azure", "octokit", "sentry",
        "graphql-tools", "apollo", "prisma", "trpc", "tanstack",
        "reduxjs", "remix-run", "next", "vercel", "netlify",
        "radix-ui", "headlessui", "floating-ui", "dnd-kit",
        "fortawesome", "mantine", "chakra-ui", "ant-design",
        "opentelemetry", "grpc", "bufbuild", "connectrpc",
        "smithy", "aws-lite", "serverless",
        "tensorflow", "huggingface", "langchain",
    ]
    
    suffixes = [
        "core", "cli", "utils", "types", "runtime", "compiler",
        "plugin", "loader", "preset", "config", "server", "client",
        "node", "browser", "common", "shared", "base", "adapter",
        "middleware", "handler", "provider", "factory", "builder",
        "parser", "formatter", "linter", "checker",
        "react", "vue", "angular", "svelte",
        "express", "koa", "fastify", "hapi",
        "jest", "mocha", "vitest",
        "webpack", "rollup", "vite", "esbuild",
        "s3", "dynamodb", "lambda", "sqs", "sns", "ec2",
        "storage", "firestore", "auth", "functions",
    ]
    
    names = set()
    for _ in range(count * 2):
        if len(names) >= count:
            break
        scope = random.choice(scopes)
        suffix = random.choice(suffixes)
        name = f"@{scope}/{suffix}"
        names.add(name)
    
    return list(names)[:count]


def generate_path_like_lib_names(count):
    """Generate path-like library folder names that appear inside dependency dirs."""
    names = set()
    all_libs = PYPI_PACKAGES + NPM_PACKAGES
    
    prefixes = ["node_modules/", "vendor/", "site-packages/", "lib/python3.11/",
                "lib/python3.10/", "lib/python3.9/", ".venv/lib/",
                "packages/", "gems/", "go/pkg/mod/"]
    
    for _ in range(count * 2):
        if len(names) >= count:
            break
        prefix = random.choice(prefixes)
        lib = random.choice(all_libs)
        name = f"{prefix}{lib}"
        names.add(name.lower())
    
    return list(names)[:count]


def main():
    print("Generating library folder names dataset...")
    
    # ================================================================
    # LIBRARY entries (is_library = 1)
    # ================================================================
    library_entries = []
    
    # 1. Real PyPI package names
    pypi = deduplicate(PYPI_PACKAGES)
    for name in pypi:
        library_entries.append((name, 1))
    print(f"  PyPI packages: {len(pypi)}")
    
    # 2. Real npm package names
    npm = deduplicate(NPM_PACKAGES)
    for name in npm:
        library_entries.append((name, 1))
    print(f"  npm packages: {len(npm)}")
    
    # 3. Java libraries
    java = deduplicate(JAVA_LIBRARIES)
    for name in java:
        library_entries.append((name, 1))
    print(f"  Java libraries: {len(java)}")
    
    # 4. Ruby gems
    ruby = deduplicate(RUBY_GEMS)
    for name in ruby:
        library_entries.append((name, 1))
    print(f"  Ruby gems: {len(ruby)}")
    
    # 5. Go modules
    go = deduplicate(GO_MODULES)
    for name in go:
        library_entries.append((name, 1))
    print(f"  Go modules: {len(go)}")
    
    # 6. Rust crates
    rust = deduplicate(RUST_CRATES)
    for name in rust:
        library_entries.append((name, 1))
    print(f"  Rust crates: {len(rust)}")
    
    # 7. C/C++ libraries
    cpp = deduplicate(C_CPP_LIBRARIES)
    for name in cpp:
        library_entries.append((name, 1))
    print(f"  C/C++ libraries: {len(cpp)}")
    
    # 8. Known library directories
    lib_dirs = deduplicate(LIBRARY_DIRECTORIES)
    for name in lib_dirs:
        library_entries.append((name, 1))
    print(f"  Library directories: {len(lib_dirs)}")
    
    # 9. Scoped npm names
    scoped = generate_scoped_npm_names(3000)
    for name in scoped:
        library_entries.append((name, 1))
    print(f"  Scoped npm names: {len(scoped)}")
    
    # 10. Synthetic library names
    synthetic_libs = generate_synthetic_library_names(35000)
    for name in synthetic_libs:
        library_entries.append((name, 1))
    print(f"  Synthetic library names: {len(synthetic_libs)}")
    
    # 11. Path-like library references
    path_libs = generate_path_like_lib_names(5000)
    for name in path_libs:
        library_entries.append((name, 1))
    print(f"  Path-like library names: {len(path_libs)}")
    
    # 12. Versioned library names
    all_base_libs = deduplicate(PYPI_PACKAGES + NPM_PACKAGES + JAVA_LIBRARIES +
                                RUBY_GEMS + GO_MODULES + RUST_CRATES + C_CPP_LIBRARIES)
    versioned = generate_versioned_lib_names(all_base_libs, 5000)
    for name in versioned:
        library_entries.append((name, 1))
    print(f"  Versioned library names: {len(versioned)}")
    
    # Deduplicate library entries
    seen_lib = set()
    unique_lib = []
    for name, label in library_entries:
        key = name.lower().strip()
        if key not in seen_lib:
            seen_lib.add(key)
            unique_lib.append((name, label))
    library_entries = unique_lib
    print(f"\n  Total unique library entries: {len(library_entries)}")
    
    # ================================================================
    # NON-LIBRARY entries (is_library = 0)
    # ================================================================
    nonlib_entries = []
    
    # 1. Common project directories
    proj = deduplicate(PROJECT_DIRS)
    for name in proj:
        if name.lower() not in seen_lib:
            nonlib_entries.append((name, 0))
    print(f"  Project directories: {len(proj)}")
    
    # 2. Business directories
    biz = deduplicate(BUSINESS_DIRS)
    for name in biz:
        if name.lower() not in seen_lib:
            nonlib_entries.append((name, 0))
    print(f"  Business directories: {len(biz)}")
    
    # 3. Dev tool directories
    dev = deduplicate(DEV_DIRS)
    for name in dev:
        if name.lower() not in seen_lib:
            nonlib_entries.append((name, 0))
    print(f"  Dev tool directories: {len(dev)}")
    
    # 4. Personal directories
    personal = deduplicate(PERSONAL_DIRS)
    for name in personal:
        if name.lower() not in seen_lib:
            nonlib_entries.append((name, 0))
    print(f"  Personal directories: {len(personal)}")
    
    # 5. Synthetic non-library names
    synthetic_nonlib = generate_synthetic_nonlib_names(35000)
    for name in synthetic_nonlib:
        if name.lower() not in seen_lib:
            nonlib_entries.append((name, 0))
    print(f"  Synthetic non-library names: {len(synthetic_nonlib)}")
    
    # Deduplicate non-library entries
    seen_nonlib = set()
    unique_nonlib = []
    for name, label in nonlib_entries:
        key = name.lower().strip()
        if key not in seen_nonlib and key not in seen_lib:
            seen_nonlib.add(key)
            unique_nonlib.append((name, label))
    nonlib_entries = unique_nonlib
    print(f"\n  Total unique non-library entries: {len(nonlib_entries)}")
    
    # ================================================================
    # COMBINE AND BALANCE
    # ================================================================
    all_entries = library_entries + nonlib_entries
    print(f"\n  Combined entries: {len(all_entries)}")
    
    # If we need more to reach 100000, generate additional synthetic entries
    target = 105000
    if len(all_entries) < target:
        needed = target - len(all_entries)
        lib_needed = needed // 2
        nonlib_needed = needed - lib_needed
        
        print(f"\n  Need {needed} more entries to reach {target}...")
        
        # Generate more synthetic library names with more variation
        extra_libs = set()
        tech_words = [
            "quantum", "neural", "vector", "tensor", "matrix", "graph",
            "pixel", "voxel", "mesh", "vertex", "node", "edge",
            "atom", "molecule", "cell", "genome", "protein", "enzyme",
            "wave", "signal", "spectrum", "frequency", "amplitude",
            "photon", "electron", "proton", "neutron", "quark",
            "cluster", "shard", "replica", "partition", "segment",
            "block", "chunk", "slice", "frame", "packet",
            "token", "cipher", "digest", "nonce", "salt",
            "beacon", "probe", "scanner", "sensor", "detector",
            "reactor", "emitter", "collector", "observer", "monitor",
            "bridge", "tunnel", "gateway", "relay", "hub",
            "forge", "anvil", "hammer", "chisel", "wrench",
            "atlas", "compass", "beacon", "lighthouse", "radar",
            "falcon", "hawk", "eagle", "swift", "sparrow",
            "wolf", "bear", "tiger", "lion", "panther",
            "storm", "thunder", "lightning", "blaze", "frost",
            "crystal", "diamond", "ruby", "emerald", "sapphire",
            "cobalt", "titanium", "carbon", "silicon", "zinc",
            "apex", "zenith", "summit", "peak", "vertex",
            "nova", "stellar", "cosmic", "nebula", "pulsar",
            "flux", "spark", "bolt", "arc", "beam",
            "zen", "karma", "mantra", "sutra", "prana",
            "pixel", "retina", "canvas", "sketch", "brush",
            "scroll", "codex", "tome", "grimoire", "oracle",
        ]
        
        lib_name_formats = [
            lambda: f"{random.choice(LIB_PREFIXES)}-{random.choice(tech_words)}",
            lambda: f"{random.choice(tech_words)}-{random.choice(LIB_SUFFIXES)}",
            lambda: f"{random.choice(tech_words)}{random.choice(LIB_SUFFIXES)}",
            lambda: f"{random.choice(LIB_PREFIXES)}{random.choice(tech_words)}",
            lambda: f"{random.choice(tech_words)}_{random.choice(tech_words)}",
            lambda: f"{random.choice(tech_words)}-{random.choice(tech_words)}-{random.choice(LIB_SUFFIXES)}",
            lambda: f"{random.choice(LIB_PREFIXES)}-{random.choice(tech_words)}-{random.choice(tech_words)}",
            lambda: f"@{random.choice(tech_words)}/{random.choice(LIB_SUFFIXES)}",
            lambda: f"{random.choice(tech_words)}.{random.choice(LIB_SUFFIXES)}",
            lambda: f"{random.choice(tech_words)}{random.randint(2, 99)}",
        ]
        
        attempts = 0
        while len(extra_libs) < lib_needed and attempts < lib_needed * 10:
            attempts += 1
            fmt = random.choice(lib_name_formats)
            name = fmt().lower()
            if name not in seen_lib and name not in seen_nonlib:
                extra_libs.add(name)
                seen_lib.add(name)
        
        for name in extra_libs:
            all_entries.append((name, 1))
        print(f"  Extra synthetic library names: {len(extra_libs)}")
        
        # Generate more non-library names
        extra_nonlib = set()
        nonlib_formats = [
            lambda: f"{random.choice(['my', 'our', 'the', 'new', 'old', 'test', 'dev', 'prod', 'staging'])}-{random.choice(NON_LIB_SUFFIXES)}-{random.randint(1, 99)}",
            lambda: f"{random.choice(NON_LIB_PREFIXES)}-{random.choice(['sprint', 'release', 'hotfix', 'bugfix', 'feature', 'refactor', 'cleanup'])}-{random.randint(1, 999)}",
            lambda: f"{random.choice(['jan', 'feb', 'mar', 'apr', 'may', 'jun', 'jul', 'aug', 'sep', 'oct', 'nov', 'dec'])}-{random.randint(2020, 2026)}-{random.choice(['release', 'sprint', 'milestone', 'update', 'patch'])}",
            lambda: f"{random.choice(['team', 'squad', 'pod', 'crew', 'unit'])}-{random.choice(['alpha', 'beta', 'gamma', 'delta', 'epsilon', 'zeta', 'theta', 'omega'])}",
            lambda: f"{random.choice(PROJECT_DIRS[:80])}-{random.choice(['v2', 'new', 'old', 'backup', 'copy', 'temp', 'archive', 'draft'])}",
            lambda: f"{''.join(random.choices(string.ascii_lowercase, k=random.randint(3, 8)))}-{''.join(random.choices(string.ascii_lowercase, k=random.randint(3, 8)))}",
            lambda: f"{random.choice(NON_LIB_PREFIXES)}_{random.choice(NON_LIB_SUFFIXES)}_{random.randint(1, 50)}",
            lambda: f"{random.choice(['get', 'set', 'create', 'update', 'delete', 'list', 'find', 'search', 'check', 'validate', 'process', 'handle', 'manage', 'sync'])}_{random.choice(['users', 'orders', 'products', 'items', 'records', 'entries', 'logs', 'events', 'tasks', 'jobs', 'files', 'docs'])}",
        ]
        
        attempts = 0
        while len(extra_nonlib) < nonlib_needed and attempts < nonlib_needed * 10:
            attempts += 1
            fmt = random.choice(nonlib_formats)
            name = fmt().lower()
            if name not in seen_lib and name not in seen_nonlib:
                extra_nonlib.add(name)
                seen_nonlib.add(name)
        
        for name in extra_nonlib:
            all_entries.append((name, 0))
        print(f"  Extra synthetic non-library names: {len(extra_nonlib)}")
    
    # ================================================================
    # SHUFFLE AND WRITE
    # ================================================================
    random.shuffle(all_entries)
    
    output_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "library_folders_dataset.csv")
    
    with open(output_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["folder_name", "is_library"])
        for name, label in all_entries:
            writer.writerow([name, label])
    
    # Stats
    total = len(all_entries)
    libs = sum(1 for _, l in all_entries if l == 1)
    nonlibs = sum(1 for _, l in all_entries if l == 0)
    
    print(f"\n{'='*60}")
    print(f"Dataset generated: {output_path}")
    print(f"{'='*60}")
    print(f"  Total entries:     {total:,}")
    print(f"  Library (1):       {libs:,} ({100*libs/total:.1f}%)")
    print(f"  Non-library (0):   {nonlibs:,} ({100*nonlibs/total:.1f}%)")
    print(f"  File size:         {os.path.getsize(output_path) / 1024 / 1024:.2f} MB")


if __name__ == "__main__":
    main()
