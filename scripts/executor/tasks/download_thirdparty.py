import os
import csv
import stat
import tarfile
import zipfile
import hashlib
import argparse
import subprocess
import urllib.request


def download_thirdparty(config_path, download_path):
    """主函数"""
    config_path = os.path.abspath(config_path)
    if not os.path.exists(config_path):
        raise FileNotFoundError(f"找不到配置文件: {config_path}")

    download_path = os.path.abspath(download_path)
    os.makedirs(download_path, exist_ok=True)

    reader = csv.DictReader(open(config_path, mode="r", encoding="utf-8"))
    configs = list(reader)  # name, version, module, repo, sha256

    for config in configs:
        archive_name = config['repo'].split('/')[-1]
        package_name = archive_name.replace(".tar.gz", "").replace(".tar", "").replace(".zip", "")
        archive_path = os.path.join(download_path, archive_name)  # vendor/xxx-vvv.zip
        vendor_path = os.path.join(download_path, config['name'])  # vendor/xxx

        if os.path.exists(vendor_path):
            print(f"依赖 {config['name']}-{config['version']} 已存在，自动跳过下载解压操作")
            continue

        print(f"正在下载 {config['name']}-{config['version']} 校验值：{config['sha256']} 下载源：{config['repo']}")
        download_zipfile(package_name, config['repo'], archive_path)
        verify_checksum(package_name, archive_path, config['sha256'])
        extract_name = extract_file(archive_path, download_path)
        package_path = os.path.join(download_path, extract_name)  # vendor/xxx-vvv
        os.rename(package_path, vendor_path)
    return 0


def download_zipfile(package_name, download_url, download_path):
    """下载文件到指定路径"""
    try:
        file = open(download_path, "wb")
        headers = {'User-Agent': 'curl/7.68.0'}
        req = urllib.request.Request(download_url, headers=headers, method='GET')
        resp = urllib.request.urlopen(req)
        file_size = int(resp.getheader('Content-Length', 0))
        downloaded = 0
        block_size = 8192

        while True:
            buffer = resp.read(block_size)
            if not buffer:
                break
            downloaded += len(buffer)
            file.write(buffer)
        file.close()
        print(f"依赖 {package_name} 下载成功：共计 {downloaded}/{file_size} 字节")
    except Exception as e:
        print(f"依赖 {package_name} 下载失败：{str(e)}")
        raise


def verify_checksum(package_name, file_path, expected_sha256):
    """验证文件校验和"""
    actual_sha256 = compute_sha256(file_path)
    if actual_sha256 == expected_sha256:
        print(f"依赖 {package_name} 哈希校验成功，哈希值：{expected_sha256}")
    else:
        print(f"依赖 {package_name} 哈希校验失败。预期值: {expected_sha256}, 实际值: {actual_sha256}")
        raise ValueError("下载校验失败")


def compute_sha256(file_path):
    """计算文件的SHA256校验值"""
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()


def extract_file(archive_path, extract_to):
    if archive_path.endswith(".zip"):
        return extract_zip(archive_path, extract_to)
    elif archive_path.endswith(".tar.gz"):
        return extract_tar(archive_path, extract_to)
    elif archive_path.endswith(".tar"):
        return extract_tar(archive_path, extract_to)
    else:
        raise TypeError(f"不支持文件 {archive_path.split('/')[-1]} 的压缩类型")


def extract_zip(zipfile_path, extract_to):
    """解压ZIP文件到指定目录"""
    zip_ref = zipfile.ZipFile(zipfile_path, 'r')
    zip_ref.extractall(extract_to)
    for info in zip_ref.infolist():
        extracted_path = os.path.join(extract_to, info.filename)
        if not info.is_dir() and info.external_attr:
            unix_permissions = (info.external_attr >> 16) & 0o777
            os.chmod(extracted_path, unix_permissions)
    for info in zip_ref.infolist():
        file_path = os.path.join(extract_to, info.filename)
        if _is_symlink(info):
            # 读取并创建符号链接
            link_target = zip_ref.read(info.filename).decode('utf-8').strip()
            if os.path.lexists(file_path):
                os.remove(file_path)
            os.symlink(link_target, file_path)
    print(f"文件 {zipfile_path.split('/')[-1]} 解压完成")
    return zip_ref.infolist()[0].filename.split('/')[0]

def extract_tar(tarfile_path, extract_to):
    """解压ZIP文件到指定目录"""
    tar_ref = tarfile.open(tarfile_path, 'r:*')
    tar_ref.extractall(extract_to)
    print(f"文件 {tarfile_path.split('/')[-1]} 解压完成")
    return tar_ref.getnames()[0]

def _is_symlink(info):
    """检查 ZIP 条目是否为符号链接"""
    mode = (info.external_attr >> 16) & 0xFFFF
    return stat.S_IFMT(mode) == stat.S_IFLNK

if __name__ == "__main__":
    _parser = argparse.ArgumentParser(description='编译前三方件下载程序')
    _parser.add_argument('--config', type=str, required=True, help='配置文件路径')
    _parser.add_argument('--output', type=str, required=True, help='依赖下载路径')
    _args = _parser.parse_args()
    _code = download_thirdparty(_args.config, _args.output)
    exit(_code)
