package customdb.chapter06.Planner;

import customdb.chapter06.DB.Schema;
import customdb.chapter06.Parser.Statement;
import customdb.chapter06.Plan.IndexScanPlan;
import customdb.chapter06.Plan.Plan;
import customdb.chapter06.Plan.SeqScanPlan;

// AST を実行計画へ変換する
public class Planner {

  public Plan createPlan(Statement.Select statement, Schema schema) {
    Statement.Condition condition = statement.whereCondition();

    // WHERE句がない
    if (condition == null) {
      return new SeqScanPlan(statement.tableName(), null);
    }

    Schema.Column column = schema.getColumn(condition.left());

    if (column != null
        && column.isIndexed()
        && column.type() == Schema.DataType.INTEGER
        && condition.operator().equals("=")) {
      return new IndexScanPlan(statement.tableName(), condition);
    }

    // それ以外は全件検索
    return new SeqScanPlan(statement.tableName(), condition);
  }
}
