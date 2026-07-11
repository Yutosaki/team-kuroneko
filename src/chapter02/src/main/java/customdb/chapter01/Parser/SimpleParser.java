package customdb.chapter01.Parser;

/** SimpleParser クラス SQLコマンド文字列を解析してトークンに分割し、 最初のトークンをコマンドとして抽出する機能を提供する */
public class SimpleParser {

  /** コンストラクタ 将来的に拡張可能な設計になっている */
  public SimpleParser() {
    // Constructor can be expanded if needed
  }

  /**
   * SQLコマンド文字列をトークンに分割する 空白で区切られたトークンに分割し、最初のトークンを小文字に変換する
   *
   * @param sql パースするSQLコマンド文字列
   * @return 分割されたトークンの配列
   */
  public String[] parse(String sql) {
    // Simple parsing logic for demonstration purposes
    String[] tokens = sql.trim().split("\\s+");
    tokens[0] = tokens[0].toLowerCase();
    return tokens;
  }

  /**
   * トークン配列から最初のトークン（コマンド）を取得する トークン配列が空の場合は空文字列を返す
   *
   * @param tokens パース済みのトークン配列
   * @return 最初のトークン、またはトークンが無い場合は空文字列
   */
  public String getCommand(String[] tokens) {
    return tokens.length > 0 ? tokens[0] : "";
  }
}
