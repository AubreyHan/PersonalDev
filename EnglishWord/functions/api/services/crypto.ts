import { SignJWT, jwtVerify } from 'jose';

const PBKDF2_ITERATIONS = 100000;
const KEY_LEN_BYTES = 32;

/**
 * 将 ArrayBuffer 转为十六进制字符串
 */
function bufferToHex(buffer: ArrayBuffer): string {
  const bytes = new Uint8Array(buffer);
  let hex = '';
  for (let i = 0; i < bytes.length; i++) {
    hex += bytes[i].toString(16).padStart(2, '0');
  }
  return hex;
}

/**
 * 将十六进制字符串转为 Uint8Array
 */
function hexToBuffer(hex: string): Uint8Array {
  const bytes = new Uint8Array(hex.length / 2);
  for (let i = 0; i < hex.length; i += 2) {
    bytes[i / 2] = parseInt(hex.substring(i, i + 2), 16);
  }
  return bytes;
}

/**
 * 基于 PBKDF2-SHA256 生成密码安全哈希 (格式: salt_hex:hash_hex)
 */
export async function hashPassword(password: string): Promise<string> {
  const enc = new TextEncoder();
  const salt = crypto.getRandomValues(new Uint8Array(16));

  const keyMaterial = await crypto.subtle.importKey(
    'raw',
    enc.encode(password),
    { name: 'PBKDF2' },
    false,
    ['deriveBits']
  );

  const derivedBits = await crypto.subtle.deriveBits(
    {
      name: 'PBKDF2',
      salt,
      iterations: PBKDF2_ITERATIONS,
      hash: 'SHA-256',
    },
    keyMaterial,
    KEY_LEN_BYTES * 8
  );

  const saltHex = bufferToHex(salt.buffer);
  const hashHex = bufferToHex(derivedBits);

  return `${saltHex}:${hashHex}`;
}

/**
 * 校验明文密码是否与存储的 salt:hash 匹配
 */
export async function verifyPassword(password: string, storedHash: string): Promise<boolean> {
  const parts = storedHash.split(':');
  if (parts.length !== 2) return false;

  const [saltHex, expectedHashHex] = parts;
  const salt = hexToBuffer(saltHex);
  const enc = new TextEncoder();

  const keyMaterial = await crypto.subtle.importKey(
    'raw',
    enc.encode(password),
    { name: 'PBKDF2' },
    false,
    ['deriveBits']
  );

  const derivedBits = await crypto.subtle.deriveBits(
    {
      name: 'PBKDF2',
      salt: salt as any,
      iterations: PBKDF2_ITERATIONS,
      hash: 'SHA-256',
    },
    keyMaterial,
    KEY_LEN_BYTES * 8
  );

  const actualHashHex = bufferToHex(derivedBits);
  return actualHashHex === expectedHashHex;
}

const DEFAULT_JWT_SECRET = 'english-word-dev-secret-key-32chars-min!!';

/**
 * 签发 JWT
 */
export async function signJwt(
  payload: Record<string, unknown>,
  secret: string = DEFAULT_JWT_SECRET,
  expiresIn: string = '7d'
): Promise<string> {
  const secretKey = new TextEncoder().encode(secret || DEFAULT_JWT_SECRET);
  return new SignJWT(payload)
    .setProtectedHeader({ alg: 'HS256' })
    .setIssuedAt()
    .setExpirationTime(expiresIn)
    .sign(secretKey);
}

/**
 * 验证并解析 JWT
 */
export async function verifyJwt<T = Record<string, unknown>>(
  token: string,
  secret: string = DEFAULT_JWT_SECRET
): Promise<T | null> {
  try {
    const secretKey = new TextEncoder().encode(secret || DEFAULT_JWT_SECRET);
    const { payload } = await jwtVerify(token, secretKey);
    return payload as unknown as T;
  } catch {
    return null;
  }
}
