package customdb.chapter06.Executor;

import customdb.chapter06.Parser.Statement;
import java.io.IOException;

public interface ExecutionContext {
  void executeSeqScan(Statement.Select statement) throws IOException;

  void executeIndexScan(Statement.Select statement) throws IOException;
}
