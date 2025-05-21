
# Multithreaded Layer 7 Load Balancer in C

A high-performance, multithreaded HTTP (Layer 7) load balancer written in C for Linux. It uses POSIX sockets and pthreads to efficiently distribute HTTP requests across multiple backend servers using round-robin scheduling, with dynamic DNS resolution for backend hosts.

---

## Demo

**[Deployed on AWS](http://ec2-13-203-231-9.ap-south-1.compute.amazonaws.com:3000/)**


**[Demo Video (takes sometime to load or goto demo_video)](demo_video/loadbalancergif.gif)**  



---

## Features

- **Layer 7 (HTTP) Load Balancing:** Forwards HTTP requests and responses between clients and backend servers.
- **Round-Robin Scheduling:** Distributes connections evenly across all backends.
- **Dynamic DNS Resolution:** Resolves backend hostnames on each connection.
- **Multithreading:** Handles each client in a separate thread.
- **Large Buffer Support:** Handles large HTTP requests/responses (up to 1MB by default).

---

## Build \& Run

### Clone the Repository

First, clone this repository to your local machine:

```sh
git clone https://github.com/harsh-panchal-804/Load-Balancer-in-C
cd Load-Balancer-in-C

```



### Prerequisites

- GCC
- Linux (POSIX-compliant) or WSL
- pthreads library


### Build using Make

You can use the following Makefile:

```makefile
main: main.c
	gcc -Wall -Wextra main.c -o main -lpthread

clean:
	rm -rf main
```

Build the project:

```sh
make
```

Clean the build:

```sh
make clean
```


### Run

```sh
./main
```

By default, the load balancer listens on port 3000 and distributes requests to three backends (see configuration in `main.c`).

---

## Configuration

Edit these arrays in `main.c` to configure Backend Servers:

```c
These specify the Domain Names and Ports of your Backend Servers.
In this project, the three Backends each serve a 3D Three.js website hosted on an AWS EC2 instance.

const char *backends_ip[] = {
    "backend1.example.com",
    "backend2.example.com",
    "backend3.example.com"
};
const int backend_port[] = { 3001, 3002, 3003 };
```


---

## Usage Example

```sh
curl http://localhost:3000/
```

The load balancer will forward the request to one of the backends and relay the response.

---

## Theory: What is a Layer 7 Load Balancer?

A **Layer 7 load balancer** operates at the application layer of the OSI model—the highest and most context-aware layer. It makes routing decisions based on the actual content of each message, such as HTTP headers, URLs, cookies, and even message bodies. This enables advanced features like:

- Content-based routing (e.g., directing image requests to specific servers)
- Application-aware health checks
- Session persistence (“sticky sessions”) using cookies or other identifiers
- SSL/TLS termination and offloading


Layer 7 load balancers act as **Reverse Proxies**, terminating the client connection, inspecting and processing the request, and then establishing a new connection to the selected backend server. This allows for granular control and sophisticated traffic management, making them ideal for modern web applications and microservices architectures.

### Layer 7 vs. Layer 4 Load Balancing

| Feature | Layer 4 Load Balancer | Layer 7 Load Balancer |
| :-- | :-- | :-- |
| **OSI Layer** | Transport Layer (TCP/UDP) | Application Layer (HTTP, HTTPS, SMTP, DNS, etc.) |
| **Routing Criteria** | IP addresses, TCP/UDP ports | HTTP headers, URLs, cookies, message content |
| **Traffic Visibility** | No application awareness | Full application awareness |
| **Performance** | Very fast, low latency | Slightly slower (content inspection overhead) |
| **Security \& Flexibility** | Lower resource use, less flexible | More resource use, highly flexible |

- **Layer 4 load balancers** operate at the transport layer, making decisions based solely on source/destination IP addresses and TCP/UDP ports. They do not inspect the actual content of the traffic, which allows for very fast and efficient packet forwarding but limits their ability to make intelligent routing decisions.
- **Layer 7 load balancers** analyze the content of each request, enabling them to route traffic based on application-specific data and provide advanced features. This comes with a small performance cost due to the deeper inspection, but modern hardware and optimizations have minimized this gap.


---


## Contributing

Pull requests and suggestions are welcome! Please open an issue to discuss changes or improvements.

---


