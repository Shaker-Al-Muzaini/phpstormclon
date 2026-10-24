//! Bounded, local stdio database client. Credentials are never logged or passed in argv.
use anyhow::{Result, bail};
use futures_util::{SinkExt, StreamExt};
use rpc_common::{RpcRequest, RpcResponse, codec::LspFrameCodec};
use serde_json::{Value, json};
use sqlx::{AnyConnection, Column, Connection, Executor, Row, TypeInfo, ValueRef};
use std::time::Duration;
use tokio_util::codec::{FramedRead, FramedWrite};

const MAX_ROWS: usize = 500;
const MAX_BYTES: usize = 4 * 1024 * 1024;
#[derive(Default)]
struct Database {
    connection: Option<AnyConnection>,
    backend: String,
    read_only: bool,
}
impl Database {
    async fn connect(&mut self, url: &str, read_only: bool) -> Result<Value> {
        self.connection = None;
        let backend = if url.starts_with("sqlite:") {
            "sqlite"
        } else if url.starts_with("mysql:") {
            "mysql"
        } else if url.starts_with("postgres:") || url.starts_with("postgresql:") {
            "postgres"
        } else {
            bail!("Use a sqlite:, mysql: or postgresql: connection URL.")
        };
        // Never downgrade network authentication to plaintext transport.
        if backend != "sqlite" {
            let parsed =
                url::Url::parse(url).map_err(|_| anyhow::anyhow!("Invalid connection URL."))?;
            let options: Vec<_> = parsed
                .query_pairs()
                .filter(|(key, _)| key == "sslmode" || key == "ssl-mode")
                .collect();
            let verified = options.len() == 1
                && options[0].1
                    == if backend == "mysql" {
                        "verify_identity"
                    } else {
                        "verify-full"
                    };
            let loopback = matches!(
                parsed.host_str(),
                Some("localhost" | "127.0.0.1" | "[::1]" | "::1")
            );
            let explicit_local = loopback
                && options.len() == 1
                && (options[0].1 == "disable" || options[0].1 == "disabled");
            if !verified && !explicit_local {
                bail!(
                    "Use ssl-mode=verify_identity for MySQL or sslmode=verify-full for PostgreSQL. Explicit TLS disable is allowed only for loopback connections."
                );
            }
        }
        let connection =
            tokio::time::timeout(Duration::from_secs(10), AnyConnection::connect(url)).await;
        // SQLx errors may contain the original URL; never forward them to the UI or log.
        let mut connection = match connection {
            Ok(Ok(connection)) => connection,
            _ => bail!(
                "Connection failed. Check the host, database, credentials, TLS certificate and server availability."
            ),
        };
        if read_only {
            let statement = match backend {
                "sqlite" => "PRAGMA query_only = ON",
                "mysql" => "SET SESSION TRANSACTION READ ONLY",
                _ => "SET default_transaction_read_only = on",
            };
            connection
                .execute(statement)
                .await
                .map_err(|_| anyhow::anyhow!("Could not enable a read-only session."))?;
        }
        self.backend = backend.into();
        self.read_only = read_only;
        self.connection = Some(connection);
        Ok(json!({"connected": true, "backend": backend, "readOnly": read_only}))
    }
    async fn query(&mut self, sql: &str) -> Result<Value> {
        validate_statement(sql, self.read_only)?;
        let connection = self
            .connection
            .as_mut()
            .ok_or_else(|| anyhow::anyhow!("Connect first."))?;
        let query = read_query(connection, sql);
        match tokio::time::timeout(Duration::from_secs(15), query).await {
            Ok(result) => result,
            Err(_) => {
                // Dropping a query future alone is not a reliable server cancellation.
                // Drop the entire connection. For writes, the outcome must be inspected after reconnect.
                self.connection = None;
                bail!(
                    "Query timed out; connection closed. Reconnect and inspect the database before retrying a write."
                )
            }
        }
    }
    async fn tables(&mut self) -> Result<Value> {
        let sql = match self.backend.as_str() {
            "sqlite" => {
                "SELECT name, type FROM sqlite_master WHERE type IN ('table','view') ORDER BY name"
            }
            "mysql" => {
                "SELECT table_schema, table_name, table_type FROM information_schema.tables WHERE table_schema = DATABASE() ORDER BY table_name"
            }
            _ => {
                "SELECT table_schema, table_name, table_type FROM information_schema.tables WHERE table_schema NOT IN ('pg_catalog', 'information_schema') ORDER BY table_schema, table_name"
            }
        };
        self.query(sql).await
    }
}

// One statement per request. Quotes/comments are parsed so semicolons in strings work.
fn validate_statement(sql: &str, read_only: bool) -> Result<()> {
    if sql.len() > 1024 * 1024 {
        bail!("SQL exceeds the 1 MiB limit.");
    }
    let bytes = sql.as_bytes();
    let mut index = 0;
    let mut ended = false;
    let mut tokens = Vec::new();
    while index < bytes.len() {
        let byte = bytes[index];
        if byte.is_ascii_whitespace() {
            index += 1;
            continue;
        }
        if bytes[index..].starts_with(b"--") || byte == b'#' {
            while index < bytes.len() && bytes[index] != b'\n' {
                index += 1;
            }
            continue;
        }
        if bytes[index..].starts_with(b"/*") {
            index += 2;
            if index < bytes.len() && bytes[index] == b'!' {
                bail!("Executable SQL comments are not supported.");
            }
            while index + 1 < bytes.len() && !bytes[index..].starts_with(b"*/") {
                index += 1;
            }
            if index + 1 >= bytes.len() {
                bail!("Unclosed SQL comment.");
            }
            index += 2;
            continue;
        }
        if ended {
            bail!("Run one SQL statement at a time.");
        }
        if byte == b';' {
            ended = true;
            index += 1;
            continue;
        }
        if byte == b'\'' || byte == b'"' || byte == b'`' {
            let quote = byte;
            index += 1;
            let mut closed = false;
            while index < bytes.len() {
                if bytes[index] == quote {
                    index += 1;
                    if index < bytes.len() && bytes[index] == quote {
                        index += 1;
                        continue;
                    }
                    closed = true;
                    break;
                }
                if bytes[index] == b'\\' {
                    index += 1;
                }
                index += 1;
            }
            if !closed {
                bail!("Unclosed SQL quote.");
            }
            continue;
        }
        if byte.is_ascii_alphabetic() || byte == b'_' {
            let start = index;
            while index < bytes.len()
                && (bytes[index].is_ascii_alphanumeric() || bytes[index] == b'_')
            {
                index += 1;
            }
            tokens.push(sql[start..index].to_ascii_uppercase());
            continue;
        }
        // Dollar-quoted SQL has backend-dependent semantics; reject rather than misparse it.
        if byte == b'$' {
            bail!(
                "Dollar-quoted SQL and positional parameters are not supported in this query editor."
            );
        }
        index += 1;
    }
    if tokens.is_empty() {
        bail!("Enter a SQL statement.");
    }
    if read_only {
        if !["SELECT", "WITH", "EXPLAIN", "SHOW", "DESCRIBE", "DESC"].contains(&tokens[0].as_str())
        {
            bail!(
                "This connection is read-only. Reconnect with Read only disabled to execute writes."
            );
        }
        if tokens.iter().any(|token| {
            [
                "INSERT", "UPDATE", "DELETE", "REPLACE", "DROP", "ALTER", "CREATE", "TRUNCATE",
                "INTO", "COPY", "CALL", "SET", "PRAGMA", "ATTACH", "COMMIT", "ROLLBACK",
            ]
            .contains(&token.as_str())
        }) {
            bail!("A write or session-control operation is not allowed on a read-only connection.");
        }
    }
    Ok(())
}
async fn read_query(connection: &mut AnyConnection, sql: &str) -> Result<Value> {
    let description = connection.describe(sql).await?;
    let mut columns: Vec<String> = description
        .columns()
        .iter()
        .map(|column| column.name().to_string())
        .collect();
    let mut stream = sqlx::raw_sql(sql).fetch_many(connection);

    let mut rows = Vec::new();
    let mut affected = 0_u64;
    let mut bytes = 0;
    let mut truncated = false;
    while let Some(item) = stream.next().await {
        match item? {
            sqlx::Either::Left(result) => affected += result.rows_affected(),
            sqlx::Either::Right(row) => {
                if columns.is_empty() {
                    columns = row
                        .columns()
                        .iter()
                        .map(|col| col.name().to_string())
                        .collect();
                }
                if rows.len() >= MAX_ROWS {
                    truncated = true;
                    break;
                }
                let mut cells = Vec::new();
                for index in 0..row.len() {
                    let raw = row.try_get_raw(index)?;
                    let value = if raw.is_null() {
                        Value::Null
                    } else {
                        match raw.type_info().name() {
                            "BOOL" => json!(row.try_get::<bool,_>(index)?),
                            "SMALLINT" => json!(row.try_get::<i16,_>(index)?),
                            "INTEGER" | "INT" => json!(row.try_get::<i32,_>(index)?),
                            "BIGINT" => json!(row.try_get::<i64,_>(index)?),
                            "REAL" => json!(row.try_get::<f32,_>(index)?),
                            "DOUBLE" | "FLOAT" => json!(row.try_get::<f64,_>(index)?),
                            "BLOB" | "BYTEA" => {
                                let blob = row.try_get::<Vec<u8>,_>(index)?;
                                json!(format!("<binary: {} bytes>", blob.len()))
                            }
                            _ => json!(row.try_get::<String,_>(index).map_err(|_| anyhow::anyhow!("Unsupported result type. Cast this column to text in your SELECT."))?),
                        }
                    };
                    bytes += value.to_string().len();
                    if bytes > MAX_BYTES {
                        truncated = true;
                        break;
                    }
                    cells.push(value);
                }
                if truncated {
                    break;
                }
                rows.push(cells);
            }
        }
    }
    Ok(json!({"columns": columns, "rows": rows, "rowsAffected": affected, "truncated": truncated}))
}
#[tokio::main]
async fn main() -> Result<()> {
    sqlx::any::install_default_drivers();
    let mut reader = FramedRead::new(tokio::io::stdin(), LspFrameCodec);
    let mut writer = FramedWrite::new(tokio::io::stdout(), LspFrameCodec);
    let mut database = Database::default();
    while let Some(frame) = reader.next().await {
        let frame = frame?;
        let parsed = serde_json::from_slice::<RpcRequest>(&frame);
        let response = match parsed {
            Err(_) => RpcResponse::err(None, -32700, "Invalid JSON-RPC request"),
            Ok(request) => {
                if request.jsonrpc != "2.0" {
                    RpcResponse::err(request.id, -32600, "Expected JSON-RPC 2.0")
                } else if request.method == "$/shutdown" {
                    break;
                } else if request.id.is_none() {
                    continue;
                } else {
                    let params = request.params.unwrap_or(json!({}));
                    let result = match request.method.as_str() {
                        "database/connect" => {
                            database
                                .connect(
                                    params["url"].as_str().unwrap_or(""),
                                    params["readOnly"].as_bool().unwrap_or(true),
                                )
                                .await
                        }
                        "database/query" => {
                            database.query(params["sql"].as_str().unwrap_or("")).await
                        }
                        "database/tables" => database.tables().await,
                        "database/disconnect" => {
                            database.connection = None;
                            Ok(json!({"connected": false}))
                        }
                        _ => {
                            let response = RpcResponse::err(request.id, -32601, "Unknown method");
                            writer.send(serde_json::to_vec(&response)?).await?;
                            continue;
                        }
                    };
                    match result {
                        Ok(result) => RpcResponse::ok(request.id, result)?,
                        Err(error) => RpcResponse::err(request.id, -32000, error.to_string()),
                    }
                }
            }
        };
        writer.send(serde_json::to_vec(&response)?).await?;
    }
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn statement_guard_preserves_quoted_semicolons_and_rejects_writes() {
        assert!(validate_statement("SELECT ';' AS text; -- done", true).is_ok());
        assert!(validate_statement("SELECT 1; DROP TABLE users", false).is_err());
        assert!(
            validate_statement(
                "WITH a AS (DELETE FROM users RETURNING *) SELECT * FROM a",
                true
            )
            .is_err()
        );
        assert!(validate_statement("PRAGMA query_only = OFF", true).is_err());
        assert!(validate_statement("SELECT 1 /*! UNION SELECT 2 */", true).is_err());
    }
    #[tokio::test]
    async fn sqlite_queries_unicode_null_schema_and_read_only_guards() -> Result<()> {
        sqlx::any::install_default_drivers();
        let mut database = Database::default();
        database.connect("sqlite::memory:", false).await?;
        database
            .query("CREATE TABLE users(id INTEGER, name TEXT, optional TEXT)")
            .await?;
        database
            .query("INSERT INTO users VALUES(7, 'عربي', NULL)")
            .await?;
        let result = database.query("SELECT * FROM users").await?;
        assert_eq!(result["rows"][0], json!([7, "عربي", null]));
        assert_eq!(database.tables().await?["rows"][0][0], "users");
        database.read_only = true;
        assert!(database.query("DELETE FROM users").await.is_err());
        database.read_only = false;
        let result = database.query("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1000) SELECT x FROM n").await?;
        assert_eq!(result["rows"].as_array().unwrap().len(), MAX_ROWS);
        assert_eq!(result["truncated"], true);
        Ok(())
    }
}
