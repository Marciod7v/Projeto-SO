# cloudIST

This repository contains implementation for the cloudIST operating systems project. 
The program simulates a data center that manages servers, VM types, reservations, and VM execution commands.

## Project Structure

### Core source files

- `main.c`: Program entry point, command loop, and command-line configuration.
- `parser.c` / `parser.h`: Parsing of command-line values and commands read from
  standard input.
- `datacenter.c` / `datacenter.h`: Data center configuration, VM type
  registration, reservations, execution, listing, and waits.
- `datacenter_utils.c` / `datacenter_utils.h`: VM type and reservation lookup,
  reservation validation and allocation, cleanup, and VM lifecycle helpers.
- `resources.c` / `resources.h`: Resource operations for CPU, RAM, and disk.
- `filesystem.c` / `filesystem.h`: Filesystem checks and absolute-path
  resolution.
- `constants.h`: Limits used by the data center, reservations, VM types, and
  identifiers.

aqui so para tanga mesmo 
Imagina que o teu programa é um hotel (o "datacenter"). O hotel tem quartos (servidores), e as pessoas podem reservar "mini-apartamentos" dentro desses quartos (VMs) para lá correrem um programinha delas.

Comandos para executar e compilar o primeiro exercicio
mkdir -p /tmp/teste-cloudist/inputs
mkdir -p /tmp/teste-cloudist/app-inputs
touch /tmp/teste-cloudist/app-inputs/exemplo.txt
cat > /tmp/teste-cloudist/inputs/1_teste.conf << 'EOF'
D V1 /tmp/teste-cloudist/app-inputs /bin/true 2 5 1
R minha_reserva V1 1 1
A minha_reserva
L
EOF
./cloudIST 4 32 500 8 /tmp/teste-cloudist/inputs

rm -rf /tmp/teste-cloudist /tmp/CloudIST