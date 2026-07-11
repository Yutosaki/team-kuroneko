package customdb.chapter06.Plan;

import customdb.chapter06.Executor.ExecutionContext;
import customdb.chapter06.Parser.Statement;
import java.io.IOException;

// 実行計画のインターフェース
public interface Plan {
  void execute(ExecutionContext context, Statement.Select statement) throws IOException;
}
