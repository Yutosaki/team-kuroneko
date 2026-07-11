package customdb.chapter07.Executor;

import java.io.IOException;

import customdb.chapter07.Parser.Statement;

public interface ExecutionContext {
  void executeSeqScan(Statement.Select statement) throws IOException;

  void executeIndexScan(Statement.Select statement) throws IOException;
}
