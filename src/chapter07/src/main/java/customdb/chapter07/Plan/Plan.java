package customdb.chapter07.Plan;

import java.io.IOException;

import customdb.chapter07.Executor.ExecutionContext;
import customdb.chapter07.Parser.Statement;

// 実行計画のインターフェース
public interface Plan {
  void execute(ExecutionContext context, Statement.Select statement) throws IOException;
}
