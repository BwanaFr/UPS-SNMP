from setuptools import setup, distutils

setup(
    name="ups-snmp-tools",
    version="1.0.0",
    python_requires=">=3.6",
    description="UPS-SNMP tools",
    author="Mathieu Donze",
    author_email="mathieu.donze@cern.ch",
    url="https://github.com/BwanaFr/UPS-SNMP",
    packages=["ups-snmp-tools"],
    # package_dir={"tester": SRCDIR},
    long_description="""UPS-SNMP tools suite""",
    install_requires=[
        "pyexcel>=0.7.5",
        "pyexcel-xlsx>=0.6.1",
        "requests>=2.34.2",
        "setuptools",
    ],
)
