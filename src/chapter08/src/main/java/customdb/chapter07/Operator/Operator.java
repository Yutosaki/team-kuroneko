package customdb.chapter07.Operator;

import customdb.chapter07.DB.Row;
import java.io.IOException;

public interface Operator {
  void open() throws IOException;

  Row next() throws IOException;

  void close() throws IOException;
}
