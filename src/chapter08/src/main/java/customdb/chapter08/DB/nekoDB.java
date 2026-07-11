package customdb.chapter08.DB;

import java.io.IOException;
import java.nio.file.Path;
import java.util.Scanner;

import customdb.chapter08.Executor.QueryExecutor;
import customdb.chapter08.Parser.SimpleParser;
import customdb.chapter08.Parser.Statement;

public class nekoDB {
  private final Scanner scanner;
  private final SimpleParser parser;
  private final QueryExecutor executor;

  public nekoDB() {
    this.scanner = new Scanner(System.in);
    this.parser = new SimpleParser();
    try {
      this.executor = new QueryExecutor(Path.of("data"));
    } catch (IOException e) {
      throw new RuntimeException("Failed to initialize QueryExecutor: " + e.getMessage(), e);
    }

    System.out.println("Welcome to nekoDB!");
  }

  public void start() {
    while (true) {
      System.out.print("db > ");
      System.out.flush();

      if (!scanner.hasNextLine()) {
        break;
      }

      String sql = scanner.nextLine().trim();

      if (sql.isEmpty()) {
        continue;
      }

      if (sql.equalsIgnoreCase("exit")) {
        try {
          executor.close();
        } catch (IOException e) {
          System.out.println("Error while closing database: " + e.getMessage());
        }

        System.out.println("Bye!");
        break;
      }

      long startTime = System.nanoTime();

      try {
        Statement statement = parser.parseStatement(sql);
        executor.execute(statement);
      } catch (Exception e) {
        System.out.println("Error: " + e.getMessage());
      }

      long endTime = System.nanoTime();
      double timeMs = (endTime - startTime) / 1_000_000.0;
      System.out.printf("(Executed in %.3f ms)%n", timeMs);
    }
  }
}
