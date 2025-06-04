# Simple-Communication-Stack-in-C
- A simple communication stack made with C, which consists of Layer 2, Layer 4 and Layer 5.
- This project is private, since its an exam for a subject i took at University, so it will only contain my implementation of Layer 2, where it sends packages using UDP (User Datagram Protocol).
- After sending the package, it waits to recieve an acknoledgement from the reciever which also sends it with UDP. The acknoledgement is supposed to have the opposite binary value of the sequence number the sender sent with the package.
  - This happens in Layer 4. 
- My work in this project was making the layer 2 and layer 4.
