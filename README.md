# Custom Unix Shell Implementation 🚀
  
*A minimal Unix-like shell supporting command execution, piping, redirections, background execution, and command history.*

## Overview
This project is a **custom Unix shell implementation** that supports various shell functionalities, including command execution, piping, redirections, background execution, logical operators, and command history.
## Features Implemented
### ✅ Basic Command Execution
- Supports execution of external programs (e.g., `ls`, `pwd`, `echo`, `whoami`, `cd`).

### ✅ Input and Output Redirection
- **Output Redirection (`>`):** Redirects command output to a file:  
  ```sh
  echo "Hello" > file.txt
  ```
- **Append Mode (`>>`):** Appends output to a file instead of overwriting:  
  ```sh
  echo "Hello again" >> file.txt
  ```
- **Input Redirection (`<`):** Uses a file as input for a command:  
  ```sh
  cat < file.txt
  ```

### ✅ Piping (`|`)
- Allows connecting multiple commands by passing output from one command as input to another:  
  ```sh
  ls | wc -l
  ```

### ✅ Background Execution (`&`)
- Enables running commands in the background without blocking the shell:  
  ```sh
  sleep 5 &
  ```
- Displays the process ID (`PID`) of background processes.

### ✅ Logical Operators (`&&` and `||`)
- **AND (`&&`)**: Executes the second command **only if** the first one succeeds:  
  ```sh
  ls && echo "Success"
  ```
- **OR (`||`)**: Executes the second command **only if** the first one fails:  
  ```sh
  ls nonexistent || echo "Failed"
  ```

### ✅ Command History (`!n`)
- Allows executing previous commands using `!n`, where `n` is the command number:  
  ```sh
  !1  # Executes the first command in history
  ```

## 🚀 Compile and Run
### 1️⃣ Compile the Shell
```sh
gcc sh.c -o utsh
```
### 2️⃣ Run the Shell
```sh
./utsh
```

## Example Commands
```sh
utsh$ ls
utsh$ pwd
utsh$ echo "Hello, Shell!"
utsh$ ls | wc -l
utsh$ echo "Output" > output.txt
utsh$ cat < output.txt
utsh$ sleep 5 &
utsh$ ls && echo "Command successful"
utsh$ ls nonexistent || echo "Command failed"
utsh$ !1  # Executes the first command in history
```

## Notes
- This shell does **not** support advanced features like job control (`fg`, `bg`, `kill`).
- It assumes valid input formats and does not handle deeply nested piping.

## Demo
YouTube : https://youtu.be/MRQHSn69ySQ?si=eoSTCEoJ44OBPhpJ 
<img width="765" alt="image" src="https://github.com/user-attachments/assets/b27a96fb-5053-4c02-a4ee-b41b739dd698" />


## License
This project is developed as part of **CS5460/6460 Operating Systems Assignment**.

## Credit
Developed by **Abhijeet Pachpute** 😎
