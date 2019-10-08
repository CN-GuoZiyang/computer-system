1 2 3 4 5 6

rbx = rsp = 77  :node 6
%rax = (%rsp + 8) = -12 :node 5
(%rbx + 8) = %rax = 80  :node 6+8
%rdx = (%rsp + 10) = -63    :node 4
(%rax + 8) = rdx = 64      :node 5+8
rax = rsp + 0x18 = -144   :node 3
(rdx + 8) = rax = 48    :node 4+8 
rdx = (rsp + 0x20) = 116  :node 2
(rax + 8) = rdx = 32    :node 3+8
rax = (rsp + 0x28) = 95 :node 1
rdx + 8 = rax = 16  :node 2+8

6 --> 5 --> 4 --> 3 --> 2 --> 1

node 6 : 589
node 5 : 500
node 4 : 193
node 3 : 398
node 2 : 884
node 1 : 95

2 6 5 3 4 1




node 4
node 3
node 4+8 --> node 3
node 2
node 3+8 --> node 2
node 1
node 2+8 --> node 1
node 6
node 1+8 --> node 6
node 5


4 --> 3 --> 2 --> 1 --> 6 --> 5



3 2 4 1 5 6

node 4
node 5
node 5+8 --> node 4
