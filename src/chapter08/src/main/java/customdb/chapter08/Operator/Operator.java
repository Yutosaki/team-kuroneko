package customdb.chapter08.Operator;

import java.io.IOException;

import customdb.chapter08.DB.Row;

public interface Operator {
  void open() throws IOException;

  Row next() throws IOException;

  void close() throws IOException;
}
