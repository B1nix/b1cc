int main(int argc, char **argv) {
  int fd;
  fd = creat("/tmp/b1cc-file-write.out", 420);
  write(fd, "file smoke\n", 11);
  close(fd);
  return 0;
}
