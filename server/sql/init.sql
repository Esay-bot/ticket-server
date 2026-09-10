-- ============================================================
-- 票务预约系统数据库初始化脚本
-- 表结构依据 server/db_manager.cpp 中的 SQL 语句反推:
--   user_info      INSERT INTO user_info VALUES(0, tel, name, passwd, 1)      -- 5 列
--   ticket_info    SELECT tk_id, addr, max, num, use_date FROM ticket_info   -- 5 列
--   reserve_ticket INSERT INTO reserve_ticket VALUES(0, tk_id, tel, NOW())   -- 4 列
-- 用法: mysql -u root -p < sql/init.sql
-- ============================================================

CREATE DATABASE IF NOT EXISTS Project_DB DEFAULT CHARACTER SET utf8mb4;
USE Project_DB;

CREATE TABLE IF NOT EXISTS user_info (
    id       INT AUTO_INCREMENT PRIMARY KEY,
    tel      VARCHAR(20)  NOT NULL UNIQUE,
    username VARCHAR(50)  NOT NULL,
    passwd   VARCHAR(64)  NOT NULL,
    status   INT          NOT NULL DEFAULT 1
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4;

CREATE TABLE IF NOT EXISTS ticket_info (
    tk_id    INT AUTO_INCREMENT PRIMARY KEY,
    addr     VARCHAR(100) NOT NULL,
    `max`    INT          NOT NULL DEFAULT 0,
    num      INT          NOT NULL DEFAULT 0,
    use_date DATE         NOT NULL
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4;

CREATE TABLE IF NOT EXISTS reserve_ticket (
    yd_id        INT AUTO_INCREMENT PRIMARY KEY,
    tk_id        INT NOT NULL,
    tel          VARCHAR(20) NOT NULL,
    reserve_time DATETIME NOT NULL,
    KEY idx_tel (tel),
    KEY idx_tk (tk_id)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4;

-- 种子数据: 4 条车票记录(Agent 评测场景)
--   西安-北京: 充足    西安-上海: 充足
--   西安-成都: 只剩 1 张(测"余票紧张"提示)
--   西安-广州: 售罄(测"售罄追问/替代班次推荐")
-- 重复导入前先 DROP DATABASE Project_DB (见 agent/scripts/reset_db.sh)
INSERT INTO ticket_info (addr, `max`, num, use_date) VALUES
    ('西安-北京', 100,  0, '2026-10-01'),
    ('西安-上海',  50,  0, '2026-10-02'),
    ('西安-成都',  20, 19, '2026-10-03'),
    ('西安-广州',  80, 80, '2026-10-04');
